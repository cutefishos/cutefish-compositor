// Copyright 2026 Reion Wong <reionwong@gmail.com>
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pwd.h>
#include <security/pam_appl.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <unistd.h>

namespace {
constexpr const char *kPasswordService = "cutefish-auth";
// A stack of its own, holding nothing but pam_fprintd: verifying against
// common-auth would put the reader's waiting in front of every password.
constexpr const char *kFingerprintService = "cutefish-auth-fingerprint";

struct Credential {
    std::array<char, 4097> bytes {};
    bool answered = false;

    ~Credential() { explicit_bzero(bytes.data(), bytes.size()); }
};

int conversation(int count, const pam_message **messages, pam_response **responses, void *data)
{
    if (count <= 0 || count > 32 || !messages || !responses)
        return PAM_CONV_ERR;
    auto &credential = *static_cast<Credential *>(data);
    auto *reply = static_cast<pam_response *>(calloc(count, sizeof(pam_response)));
    if (!reply)
        return PAM_BUF_ERR;
    for (int i = 0; i < count; ++i) {
        if (messages[i]->msg_style == PAM_PROMPT_ECHO_OFF && !credential.answered) {
            credential.answered = true;
            reply[i].resp = strdup(credential.bytes.data());
            if (reply[i].resp)
                continue;
        } else if (messages[i]->msg_style == PAM_TEXT_INFO
                   || messages[i]->msg_style == PAM_ERROR_MSG) {
            // The reader's progress notices ("Place your finger on the
            // reader"). They never carry a credential, and they are all this
            // program writes: the caller takes any line as the reader having
            // started.
            if (messages[i]->msg)
                fprintf(stderr, "%s\n", messages[i]->msg);
            continue;
        }
        for (int j = 0; j < count; ++j) {
            if (reply[j].resp) {
                explicit_bzero(reply[j].resp, strlen(reply[j].resp));
                free(reply[j].resp);
            }
        }
        free(reply);
        return PAM_CONV_ERR;
    }
    *responses = reply;
    return PAM_SUCCESS;
}
} // namespace

int main(int argc, char **argv)
{
    // Without a password on stdin, authenticate the reader instead. This is
    // what lets the lock screen start an attempt before anything is typed.
    bool fingerprint = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--fingerprint") == 0)
            fingerprint = true;
    }

    const rlimit limit {0, 0};
    setrlimit(RLIMIT_CORE, &limit);
    prctl(PR_SET_DUMPABLE, 0);
    alarm(35);
    const passwd *user = getpwuid(getuid());
    if (!user)
        return 1;
    Credential credential;
    size_t size = 0;
    if (!fingerprint) {
        for (;;) {
            const ssize_t n =
                    read(STDIN_FILENO, credential.bytes.data() + size, credential.bytes.size() - size);
            if (n < 0)
                return 1;
            if (!n)
                break;
            size += size_t(n);
            if (size >= credential.bytes.size())
                return 1;
        }
        if (!size || memchr(credential.bytes.data(), 0, size))
            return 1;
    }
    pam_conv conv {conversation, &credential};
    pam_handle_t *handle = nullptr;
    int result = pam_start(fingerprint ? kFingerprintService : kPasswordService, user->pw_name,
                           &conv, &handle);
    if (result == PAM_SUCCESS) {
        result = pam_authenticate(handle, PAM_DISALLOW_NULL_AUTHTOK);
        if (result == PAM_SUCCESS)
            result = pam_acct_mgmt(handle, 0);
    }
    if (handle)
        pam_end(handle, result);
    // A finger authenticates without answering a prompt: the credential the
    // conversation would have carried stays empty, and that is a success.
    // PAM_MAXTRIES is a finger that was read and did not match, which the
    // caller tells apart (2) to try again while the user is at the reader.
    if (fingerprint)
        return result == PAM_SUCCESS ? 0 : result == PAM_MAXTRIES ? 2 : 1;
    return result == PAM_SUCCESS && credential.answered ? 0 : 1;
}
