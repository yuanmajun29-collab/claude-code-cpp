#include "tools/web_fetch_tool.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <regex>
#include <algorithm>

using json = nlohmann::json;

namespace claude {

// Write callback for curl
static size_t write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    size_t total = size * nmemb;
    str->append(static_cast<char*>(contents), total);
    return total;
}

ToolInputSchema WebFetchTool::input_schema() const {
    ToolInputSchema schema;
    schema.type = "object";
    schema.properties["url"] = {"string", "HTTP(S) URL to fetch"};
    schema.properties["max_chars"] = {"number", "Maximum characters to return (default 50000)"};
    schema.required = {"url"};
    return schema;
}

std::string WebFetchTool::system_prompt() const {
    return R"(The WebFetch tool fetches a URL and returns its content as text.

**Parameters:**
- url (required): HTTP(S) URL to fetch
- max_chars: Max characters to return (default 50000)

**Behavior:**
- Fetches the page content using HTTP GET
- Strips HTML tags and converts to readable text
- Extracts the page title
- Returns markdown-style output with the URL, title, and content
- Follows redirects automatically
- 30-second timeout)";
}

std::optional<std::string> WebFetchTool::fetch_url(const std::string& url, int timeout_seconds) {
    CURL* curl = curl_easy_init();
    if (!curl) return std::nullopt;

    std::string response_body;
    std::string response_headers;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ClaudeCode-CPP/0.1.0");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    // Accept common content types
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: text/html,application/xhtml+xml,application/xml,text/plain,*/*");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        spdlog::debug("WebFetch curl error: {}", curl_easy_strerror(res));
        return std::nullopt;
    }

    if (http_code != 200) {
        spdlog::debug("WebFetch HTTP {}", http_code);
        return std::nullopt;
    }

    return response_body;
}

std::string WebFetchTool::extract_title(const std::string& html) const {
    // Match <title>...</title>
    std::regex title_rx("<title[^>]*>(.*?)</title>", std::regex::icase | std::regex::extended);
    std::smatch match;
    if (std::regex_search(html, match, title_rx) && match.size() > 1) {
        return util::trim(match[1].str());
    }
    return "";
}

std::string WebFetchTool::html_to_text(const std::string& html) const {
    std::string text;
    text.reserve(html.size());

    bool in_tag = false;
    bool in_script = false;
    bool in_style = false;

    for (size_t i = 0; i < html.size(); i++) {
        if (html[i] == '<') {
            in_tag = true;

            // Check for script/style blocks
            std::string tag;
            size_t j = i + 1;
            while (j < html.size() && (std::isalpha(html[j]) || html[j] == '/')) {
                tag += html[j++];
            }
            util::to_lower(tag);

            if (tag == "script") in_script = true;
            if (tag == "style") in_style = true;
            if (tag == "/script") in_script = false;
            if (tag == "/style") in_style = false;

            // Convert block-level tags to newlines
            if (tag == "p" || tag == "br" || tag == "div" || tag == "h1" || tag == "h2" ||
                tag == "h3" || tag == "h4" || tag == "h5" || tag == "h6" || tag == "li" ||
                tag == "tr" || tag == "hr" || tag == "blockquote" || tag == "pre" ||
                tag == "/p" || tag == "/div" || tag == "/h1" || tag == "/h2" ||
                tag == "/h3" || tag == "/h4" || tag == "/h5" || tag == "/h6" ||
                tag == "/li" || tag == "/tr" || tag == "/blockquote" || tag == "/pre") {
                if (!text.empty() && text.back() != '\n') text += '\n';
            }
            continue;
        }
        if (html[i] == '>') {
            in_tag = false;
            continue;
        }
        if (in_tag || in_script || in_style) continue;

        // Decode common HTML entities
        if (html[i] == '&') {
            std::string entity;
            size_t j = i + 1;
            while (j < html.size() && html[j] != ';' && j - i < 10) {
                entity += html[j++];
            }
            if (j < html.size() && html[j] == ';') {
                util::to_lower(entity);
                if (entity == "amp") text += '&';
                else if (entity == "lt") text += '<';
                else if (entity == "gt") text += '>';
                else if (entity == "quot") text += '"';
                else if (entity == "nbsp") text += ' ';
                else if (entity == "apos") text += '\'';
                else text += ' ';  // Unknown entity
                i = j;
                continue;
            }
        }

        // Collapse whitespace
        if (std::isspace(static_cast<unsigned char>(html[i]))) {
            if (!text.empty() && text.back() != ' ' && text.back() != '\n') {
                text += ' ';
            }
        } else {
            text += html[i];
        }
    }

    // Collapse multiple spaces/newlines
    std::string result;
    result.reserve(text.size());
    bool prev_space = false;
    bool prev_newline = false;
    for (char c : text) {
        if (c == '\n') {
            if (!prev_newline) result += c;
            prev_newline = true;
            prev_space = false;
        } else if (c == ' ') {
            if (!prev_space && !prev_newline) result += c;
            prev_space = true;
            prev_newline = false;
        } else {
            result += c;
            prev_space = false;
            prev_newline = false;
        }
    }

    return util::trim(result);
}

ToolOutput WebFetchTool::execute(const std::string& input_json, ToolContext& /*ctx*/) {
    try {
        auto j = json::parse(input_json);
        std::string url = j.value("url", "");
        size_t max_chars = j.value("max_chars", 50000);

        if (url.empty()) {
            return ToolOutput::err("No URL specified");
        }

        // Validate URL scheme
        if (!util::starts_with(url, "http://") && !util::starts_with(url, "https://")) {
            return ToolOutput::err("URL must start with http:// or https://");
        }

        spdlog::debug("WebFetch: {}", url);

        auto html = fetch_url(url);
        if (!html) {
            return ToolOutput::err("Failed to fetch URL: " + url + " (timeout, DNS error, or non-200 response)");
        }

        std::string title = extract_title(*html);
        std::string text = html_to_text(*html);

        if (text.empty()) {
            return ToolOutput::ok("Fetched " + url + " but extracted no readable content.");
        }

        // Truncate
        if (text.size() > max_chars) {
            text = text.substr(0, max_chars) + "\n\n[Content truncated at " +
                   std::to_string(max_chars) + " characters]";
        }

        std::ostringstream output;
        output << "# " << (title.empty() ? url : title) << "\n";
        output << "Source: " << url << "\n\n";
        output << text;

        return ToolOutput::ok(output.str());

    } catch (const json::parse_error& e) {
        return ToolOutput::err("Invalid JSON input: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ToolOutput::err(std::string("Error: ") + e.what());
    }
}

}  // namespace claude
