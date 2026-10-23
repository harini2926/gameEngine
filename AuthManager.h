#pragma once

#include <string>
#include <vector>

namespace NexoraAuth
{
    struct User
    {
        std::string username;
        std::string email;
        std::string passwordHash;
        std::string provider;   // "local" or "google"
    };

    struct AuthResult
    {
        bool        success = false;
        std::string message;
        User        user;
    };

    std::vector<User> LoadUsers();
    bool              SaveUsers(const std::vector<User>& users);

    bool              UsernameTaken(const std::string& username);
    bool              EmailRegistered(const std::string& email);

    AuthResult        Register(const std::string& username,
                               const std::string& email,
                               const std::string& password);

    AuthResult        Login(const std::string& identifier,
                            const std::string& password);

    AuthResult        LoginWithGoogle(const std::string& email);
    AuthResult        RegisterWithGoogle(const std::string& email,
                                         const std::string& preferredUsername);

    bool              SaveSession(const std::string& email);
    std::string       LoadSession();
    void              ClearSession();

    std::string       HashPassword(const std::string& password);
}