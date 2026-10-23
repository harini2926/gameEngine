#include "AuthManager.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    // ------------------------------------------------------------
    // File locations
    // ------------------------------------------------------------
    fs::path UsersFilePath()
    {
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");

        std::error_code ec;
        if (!home)
            return fs::current_path(ec) / "Nexora" / "users.json";

        fs::path dir = fs::path(home) / "Nexora";
        fs::create_directories(dir, ec);
        return dir / "users.json";
    }

    fs::path SessionFilePath()
    {
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");

        std::error_code ec;
        if (!home)
            return fs::current_path(ec) / "Nexora" / "session.txt";

        fs::path dir = fs::path(home) / "Nexora";
        fs::create_directories(dir, ec);
        return dir / "session.txt";
    }

    // ------------------------------------------------------------
    // String helpers
    // ------------------------------------------------------------
    std::string ToLower(std::string s)
    {
        for (auto& c : s)
            c = (char)std::tolower((unsigned char)c);
        return s;
    }

    bool ContainsAtSignAndDot(const std::string& email)
    {
        auto at  = email.find('@');
        auto dot = email.find('.', at == std::string::npos ? 0 : at);
        return at != std::string::npos &&
               dot != std::string::npos &&
               dot > at + 1 &&
               dot < email.size() - 1;
    }

    // ------------------------------------------------------------
    // JSON-ish helpers (hand-rolled, adequate for our schema)
    // ------------------------------------------------------------
    std::string EscapeJson(const std::string& s)
    {
        std::string out;
        out.reserve(s.size() + 8);
        for (unsigned char c : s)
        {
            switch (c)
            {
                case '\\': out += "\\\\"; break;
                case '"':  out += "\\\""; break;
                case '\n': out += "\\n";  break;
                case '\r': out += "\\r";  break;
                case '\t': out += "\\t";  break;
                default:
                    if (c < 0x20)
                    {
                        char buf[8];
                        std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                        out += buf;
                    }
                    else out += (char)c;
                    break;
            }
        }
        return out;
    }

    void SkipWs(const std::string& t, size_t& i)
    {
        while (i < t.size() &&
               (t[i] == ' ' || t[i] == '\t' ||
                t[i] == '\n' || t[i] == '\r'))
            ++i;
    }

    std::string ParseString(const std::string& t, size_t& i)
    {
        std::string out;
        if (i >= t.size() || t[i] != '"') return out;
        ++i;
        while (i < t.size())
        {
            char c = t[i];
            if (c == '\\' && i + 1 < t.size())
            {
                char n = t[i + 1];
                switch (n)
                {
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case '"':  out += '"';  break;
                    case '\\': out += '\\'; break;
                    default:   out += n;    break;
                }
                i += 2;
                continue;
            }
            if (c == '"') { ++i; return out; }
            out += c;
            ++i;
        }
        return out;
    }
}

namespace NexoraAuth
{
    // ============================================================
    // Password hashing
    // ============================================================
    std::string HashPassword(const std::string& password)
    {
        // FNV-1a 64-bit — deterministic, no external deps.
        // Not cryptographically strong; fine for local demos.
        std::uint64_t h = 1469598103934665603ULL;
        for (unsigned char c : password)
        {
            h ^= c;
            h *= 1099511628211ULL;
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%016llx",
                      (unsigned long long)h);
        return buf;
    }

    // ============================================================
    // User store
    // ============================================================
    std::vector<User> LoadUsers()
    {
        std::vector<User> users;
        try
        {
            fs::path p = UsersFilePath();
            std::error_code ec;
            if (!fs::exists(p, ec)) return users;

            std::ifstream in(p);
            if (!in) return users;

            std::stringstream ss;
            ss << in.rdbuf();
            std::string text = ss.str();

            size_t i = 0;
            size_t arr = text.find('[');
            if (arr == std::string::npos) return users;
            i = arr + 1;

            while (i < text.size())
            {
                SkipWs(text, i);
                if (i >= text.size()) break;
                if (text[i] == ']') break;
                if (text[i] == ',') { ++i; continue; }
                if (text[i] != '{') break;
                ++i;

                User u;
                while (i < text.size())
                {
                    SkipWs(text, i);
                    if (i >= text.size()) break;
                    if (text[i] == '}') { ++i; break; }
                    if (text[i] == ',') { ++i; continue; }
                    if (text[i] != '"') break;

                    std::string key = ParseString(text, i);

                    SkipWs(text, i);
                    if (i >= text.size() || text[i] != ':') break;
                    ++i;
                    SkipWs(text, i);
                    if (i >= text.size() || text[i] != '"') break;
                    std::string value = ParseString(text, i);

                    if      (key == "username")     u.username     = value;
                    else if (key == "email")        u.email        = value;
                    else if (key == "passwordHash") u.passwordHash = value;
                    else if (key == "provider")     u.provider     = value;
                }

                if (!u.username.empty())
                    users.push_back(std::move(u));
            }
        }
        catch (const std::exception& e)
        {
            std::cerr << "LoadUsers failed: " << e.what() << "\n";
        }
        return users;
    }

    bool SaveUsers(const std::vector<User>& users)
    {
        try
        {
            fs::path p   = UsersFilePath();
            fs::path tmp = p;
            tmp += ".tmp";

            {
                std::ofstream out(tmp, std::ios::trunc);
                if (!out) return false;

                out << "[\n";
                for (size_t i = 0; i < users.size(); ++i)
                {
                    const auto& u = users[i];
                    out << "    {\n";
                    out << "        \"username\": \""     << EscapeJson(u.username)     << "\",\n";
                    out << "        \"email\": \""        << EscapeJson(u.email)        << "\",\n";
                    out << "        \"passwordHash\": \"" << EscapeJson(u.passwordHash) << "\",\n";
                    out << "        \"provider\": \""     << EscapeJson(u.provider)     << "\"\n";
                    out << "    }";
                    if (i + 1 < users.size()) out << ",";
                    out << "\n";
                }
                out << "]\n";
            }

            std::error_code ec;
            fs::rename(tmp, p, ec);
            if (ec)
            {
                fs::remove(p, ec);
                fs::rename(tmp, p, ec);
            }
            return true;
        }
        catch (const std::exception& e)
        {
            std::cerr << "SaveUsers failed: " << e.what() << "\n";
            return false;
        }
    }

    // ============================================================
    // Uniqueness checks
    // ============================================================
    bool UsernameTaken(const std::string& username)
    {
        std::string lower = ToLower(username);
        for (const auto& u : LoadUsers())
            if (ToLower(u.username) == lower) return true;
        return false;
    }

    bool EmailRegistered(const std::string& email)
    {
        std::string lower = ToLower(email);
        for (const auto& u : LoadUsers())
            if (ToLower(u.email) == lower) return true;
        return false;
    }

    // ============================================================
    // Register (local)
    // ============================================================
    AuthResult Register(const std::string& username,
                        const std::string& email,
                        const std::string& password)
    {
        AuthResult r;

        std::string u = username;
        std::string e = email;

        // Trim
        auto trim = [](std::string& s)
        {
            size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
            s = s.substr(a, b - a);
        };
        trim(u);
        trim(e);

        if (u.empty() || e.empty() || password.empty())
        {
            r.message = "All fields are required.";
            return r;
        }
        if (u.size() < 3)
        {
            r.message = "Username must be at least 3 characters.";
            return r;
        }
        if (!ContainsAtSignAndDot(e))
        {
            r.message = "Please enter a valid email address.";
            return r;
        }
        if (password.size() < 6)
        {
            r.message = "Password must be at least 6 characters.";
            return r;
        }
        if (EmailRegistered(e))
        {
            r.message = "This email is already registered.";
            return r;
        }
        if (UsernameTaken(u))
        {
            r.message = "This username is already taken.";
            return r;
        }

        User nu;
        nu.username     = u;
        nu.email        = e;
        nu.passwordHash = HashPassword(password);
        nu.provider     = "local";

        auto users = LoadUsers();
        users.push_back(nu);
        if (!SaveUsers(users))
        {
            r.message = "Failed to save user. Try again.";
            return r;
        }

        r.success = true;
        r.user    = nu;
        r.message = "Welcome, " + nu.username + "!";
        return r;
    }

    // ============================================================
    // Login (email OR username)
    // ============================================================
    AuthResult Login(const std::string& identifier,
                     const std::string& password)
    {
        AuthResult r;

        std::string id = identifier;
        // Trim
        {
            size_t a = 0, b = id.size();
            while (a < b && (id[a] == ' ' || id[a] == '\t')) ++a;
            while (b > a && (id[b - 1] == ' ' || id[b - 1] == '\t')) --b;
            id = id.substr(a, b - a);
        }

        if (id.empty() || password.empty())
        {
            r.message = "Please enter your credentials.";
            return r;
        }

        std::string idLower = ToLower(id);
        std::string hash    = HashPassword(password);

        for (const auto& u : LoadUsers())
        {
            bool match =
                ToLower(u.email)    == idLower ||
                ToLower(u.username) == idLower;

            if (match)
            {
                if (u.provider == "google")
                {
                    r.message = "This account uses Google sign-in.";
                    return r;
                }
                if (u.passwordHash == hash)
                {
                    r.success = true;
                    r.user    = u;
                    r.message = "Welcome back, " + u.username + "!";
                    return r;
                }
            }
        }

        r.message = "Invalid username/email or password.";
        return r;
    }

    // ============================================================
    // Google login (existing Google user only)
    // ============================================================
    AuthResult LoginWithGoogle(const std::string& email)
    {
        AuthResult r;

        for (const auto& u : LoadUsers())
        {
            if (ToLower(u.email) == ToLower(email) && u.provider == "google")
            {
                r.success = true;
                r.user    = u;
                r.message = "Welcome back, " + u.username + "!";
                return r;
            }
        }

        r.message = "No Google account for " + email +
                    ". Please register with Google.";
        return r;
    }

    // ============================================================
    // Google register (creates user if email not present)
    // ============================================================
    AuthResult RegisterWithGoogle(const std::string& email,
                                  const std::string& preferredUsername)
    {
        AuthResult r;

        if (!ContainsAtSignAndDot(email))
        {
            r.message = "Please enter a valid Google email.";
            return r;
        }

        // If a user already exists for this email, log them in
        for (const auto& u : LoadUsers())
        {
            if (ToLower(u.email) == ToLower(email))
            {
                r.success = true;
                r.user    = u;
                r.message = "Welcome back, " + u.username + "!";
                return r;
            }
        }

        User nu;
        nu.email        = email;
        nu.provider     = "google";
        nu.passwordHash = "";

        std::string base = preferredUsername.empty()
            ? email.substr(0, email.find('@'))
            : preferredUsername;
        if (base.empty()) base = "user";

        std::string candidate = base;
        int suffix = 1;
        while (UsernameTaken(candidate))
            candidate = base + std::to_string(suffix++);

        nu.username = candidate;

        auto users = LoadUsers();
        users.push_back(nu);
        if (!SaveUsers(users))
        {
            r.message = "Failed to save user.";
            return r;
        }

        r.success = true;
        r.user    = nu;
        r.message = "Signed in with Google as " + nu.username;
        return r;
    }

    // ============================================================
    // Session persistence
    // ============================================================
    bool SaveSession(const std::string& email)
    {
        try
        {
            std::ofstream out(SessionFilePath(), std::ios::trunc);
            if (!out) return false;
            out << email << "\n";
            return true;
        }
        catch (...) { return false; }
    }

    std::string LoadSession()
    {
        try
        {
            std::ifstream in(SessionFilePath());
            if (!in) return {};
            std::string line;
            std::getline(in, line);
            // Trim
            size_t a = 0, b = line.size();
            while (a < b && (line[a] == ' ' || line[a] == '\t')) ++a;
            while (b > a && (line[b - 1] == ' ' || line[b - 1] == '\t')) --b;
            return line.substr(a, b - a);
        }
        catch (...) { return {}; }
    }

    void ClearSession()
    {
        std::error_code ec;
        fs::remove(SessionFilePath(), ec);
    }
}