#include "tools/web_search_tool.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <regex>

using json = nlohmann::json;

namespace claude {

// Write callback for curl
static size_t search_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    size_t total = size * nmemb;
    str->append(static_cast<char*>(contents), total);
    return total;
}

ToolInputSchema WebSearchTool::input_schema() const {
    ToolInputSchema schema;
    schema.type = "object";
    schema.properties["query"] = {"string", "Search query string"};
    schema.properties["max_results"] = {"number", "Maximum number of results (default 5)"};
    schema.required = {"query"};
    return schema;
}

std::string WebSearchTool::system_prompt() const {
    return R"(The WebSearch tool searches the web using the Brave Search API.

**Parameters:**
- query (required): Search query
- max_results: Number of results (default 5, max 20)

**Requirements:**
- Set BRAVE_API_KEY environment variable for web search
- Falls back to DuckDuckGo HTML scraping if no API key is set

**Returns:**
- Title, URL, and snippet for each result)";
}

// Brave Search API implementation
static std::string brave_search(const std::string& query, int max_results, const std::string& api_key) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    // URL encode query
    char* encoded = curl_easy_escape(curl, query.c_str(), static_cast<int>(query.size()));
    std::string url = "https://api.search.brave.com/res/v1/web/search?q=" +
                      std::string(encoded) + "&count=" + std::to_string(max_results);
    curl_free(encoded);

    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, search_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, ("X-Subscription-Token: " + api_key).c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != 200) {
        spdlog::debug("Brave search failed: {} (HTTP {})", curl_easy_strerror(res), http_code);
        return "";
    }

    return response_body;
}

// DuckDuckGo HTML fallback
static std::string ddg_search(const std::string& query, int max_results) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    char* encoded = curl_easy_escape(curl, query.c_str(), static_cast<int>(query.size()));
    std::string url = "https://html.duckduckgo.com/html/?q=" + std::string(encoded);
    curl_free(encoded);

    std::string response_body;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (compatible; ClaudeCode/0.1)");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, search_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK || http_code != 200) return "";

    // Parse HTML results - extract links and snippets
    std::ostringstream results;
    int count = 0;

    // Match result links: <a class="result__a" href="...">title</a>
    std::regex link_rx(R"RE(<a[^>]*class="result__a"[^>]*href="([^"]*)"[^>]*>(.*?)</a>)RE",
                       std::regex::icase);
    std::regex snippet_rx(R"RE(<a[^>]*class="result__snippet"[^>]*>(.*?)</a>)RE",
                          std::regex::icase);
    std::regex tag_rx("<[^>]*>");

    auto links_begin = std::sregex_iterator(response_body.begin(), response_body.end(), link_rx);
    auto snippets_begin = std::sregex_iterator(response_body.begin(), response_body.end(), snippet_rx);
    auto end = std::sregex_iterator();

    auto snip_it = snippets_begin;
    for (auto it = links_begin; it != end && count < max_results; ++it) {
        std::string url_str = (*it)[1].str();
        std::string title = std::regex_replace((*it)[2].str(), tag_rx, "");
        std::string snippet;
        if (snip_it != end) {
            snippet = std::regex_replace((*snip_it)[1].str(), tag_rx, "");
            ++snip_it;
        }

        // Decode URL (DuckDuckGo redirects)
        if (url_str.find("uddg=") != std::string::npos) {
            auto pos = url_str.find("uddg=");
            url_str = url_str.substr(pos + 5);
            auto amp = url_str.find('&');
            if (amp != std::string::npos) url_str = url_str.substr(0, amp);
            // URL decode
            CURL* dec = curl_easy_init();
            if (dec) {
                int out_len = 0;
                char* decoded = curl_easy_unescape(dec, url_str.c_str(),
                                                    static_cast<int>(url_str.size()), &out_len);
                if (decoded) {
                    url_str = std::string(decoded, out_len);
                    curl_free(decoded);
                }
                curl_easy_cleanup(dec);
            }
        }

        results << count + 1 << ". " << util::trim(title) << "\n"
                << "   URL: " << url_str << "\n";
        if (!snippet.empty()) {
            results << "   " << util::trim(snippet) << "\n";
        }
        results << "\n";
        count++;
    }

    return results.str();
}

ToolOutput WebSearchTool::execute(const std::string& input_json, ToolContext& /*ctx*/) {
    try {
        auto j = json::parse(input_json);
        std::string query = j.value("query", "");
        int max_results = j.value("max_results", 5);

        if (query.empty()) {
            return ToolOutput::err("No query specified");
        }

        max_results = std::min(max_results, 20);
        if (max_results < 1) max_results = 5;

        spdlog::debug("WebSearch: {} (max {})", query, max_results);

        // Try Brave Search API first
        const char* brave_key = getenv("BRAVE_API_KEY");
        if (brave_key && brave_key[0] != '\0') {
            auto response = brave_search(query, max_results, brave_key);
            if (!response.empty()) {
                try {
                    auto data = json::parse(response);
                    std::ostringstream results;
                    results << "Search results for: " << query << "\n\n";

                    if (data.contains("web") && data["web"].contains("results")) {
                        int count = 0;
                        for (const auto& r : data["web"]["results"]) {
                            if (count >= max_results) break;
                            results << count + 1 << ". " << r.value("title", "No title") << "\n"
                                    << "   URL: " << r.value("url", "") << "\n"
                                    << "   " << r.value("description", "") << "\n\n";
                            count++;
                        }
                        if (count > 0) {
                            return ToolOutput::ok(results.str());
                        }
                    }
                } catch (...) {
                    spdlog::debug("Failed to parse Brave search response");
                }
            }
        }

        // Fall back to DuckDuckGo HTML scraping
        auto ddg_results = ddg_search(query, max_results);
        if (!ddg_results.empty()) {
            return ToolOutput::ok("Search results for: " + query + "\n\n" + ddg_results);
        }

        return ToolOutput::err(
            "Web search failed. Set BRAVE_API_KEY environment variable for Brave Search API, "
            "or check network connectivity for DuckDuckGo fallback.");

    } catch (const json::parse_error& e) {
        return ToolOutput::err("Invalid JSON input: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ToolOutput::err(std::string("Error: ") + e.what());
    }
}

}  // namespace claude
