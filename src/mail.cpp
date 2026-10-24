#include "mail.h"
#include "spdlog/spdlog.h"

#include <unistd.h>

extern bool debugflag;

namespace mail {

// Internal callback for curl
static size_t readEmailCallback(void* ptr, size_t size, size_t nmemb, void* userp) {
    EmailData* emailData = static_cast<EmailData*>(userp);

    size_t available = emailData->content.length() - emailData->index;
    if (available == 0) {
        return 0;
    }

    size_t to_copy = std::min(available, size * nmemb);
    memcpy(ptr, emailData->content.c_str() + emailData->index, to_copy);
    emailData->index += to_copy;

    return to_copy;
}

void initCurl() { curl_global_init(CURL_GLOBAL_DEFAULT); }

void cleanupCurl() { curl_global_cleanup(); }

bool sendCurl(const std::string& smtpUrl, const std::string& mail_from, std::vector<std::string>& mail_to,
              const std::string& completeMail) {
    CURL* curl;
    CURLcode res = CURLE_OK;

    curl = curl_easy_init();
    if (!curl) {
        if (debugflag)
            spdlog::debug("Failed to initialize curl");
        return false;
    }

    EmailData emailData(completeMail);

    curl_easy_setopt(curl, CURLOPT_URL, smtpUrl.c_str());

    struct curl_slist* recipients = nullptr;
    for (const auto& mailaddress : mail_to) {
        recipients = curl_slist_append(recipients, mailaddress.c_str());
    }
    curl_easy_setopt(curl, CURLOPT_MAIL_RCPT, recipients);
    curl_easy_setopt(curl, CURLOPT_MAIL_FROM, mail_from.c_str());

    curl_easy_setopt(curl, CURLOPT_READFUNCTION, readEmailCallback);
    curl_easy_setopt(curl, CURLOPT_READDATA, &emailData);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);

    if (debugflag) {
        curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    }

    res = curl_easy_perform(curl);

    if (debugflag) {
        if (res == CURLE_OK) {
            spdlog::debug("Email sent successfully");
        } else {
            spdlog::debug("curl_easy_perform() failed: {}", curl_easy_strerror(res));
        }
    }

    curl_slist_free_all(recipients);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
}

std::string generateMailDateFormat(const time_t time) {
    // generous buffer: far-future years and localized names must not make strftime run out of space
    char timeString[128];
    struct tm tm_buf = {};
    // both calls can fail (EOVERFLOW for extreme timestamps / result does not fit); never read the
    // buffer then, and never invent a date, as callers print this in subject and body of mails
    if (localtime_r(&time, &tm_buf) == nullptr ||
        std::strftime(timeString, sizeof(timeString), "%a, %d %h %Y %X %z", &tm_buf) == 0) {
        spdlog::warn("could not format timestamp {} as mail date", time);
        return "(date out of range)";
    }
    return std::string(timeString);
}

std::string generateMessageID(const std::string& domain) {
    auto now = std::time(nullptr);
    auto pid = getpid();

    std::hash<std::string> hasher;
    std::string unique_string = std::to_string(now) + std::to_string(pid) + std::to_string(rand());
    auto hash = hasher(unique_string);

    return fmt::format("{}.{}.{}@{}", now, pid, hash, domain);
}

std::string generateToHeader(std::vector<std::string> mail_to) {
    std::string to_header;
    for (size_t i = 0; i < mail_to.size(); ++i) {
        if (i > 0)
            to_header += ", ";
        to_header += mail_to[i];
    }
    return to_header;
}

} // namespace mail
