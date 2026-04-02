#include "protocol/json_rpc.h"
#include <spdlog/spdlog.h>
#include <sstream>

namespace claude {

using json = nlohmann::json;

void JsonRpc::register_method(const std::string& method, JsonRpcMethodHandler handler) {
    handlers_[method] = std::move(handler);
}

void JsonRpc::unregister_method(const std::string& method) {
    handlers_.erase(method);
}

bool JsonRpc::has_method(const std::string& method) const {
    return handlers_.find(method) != handlers_.end();
}

std::string JsonRpc::process(const std::string& raw_message) {
    json msg;
    try {
        msg = json::parse(raw_message);
    } catch (const json::parse_error& e) {
        return build_error("", JsonRpcErrorCode::ParseError,
                           "Parse error: " + std::string(e.what()));
    }

    // Handle batch requests (array)
    if (msg.is_array()) {
        if (msg.empty()) {
            return build_error("", JsonRpcErrorCode::InvalidRequest, "Empty batch");
        }
        json responses = json::array();
        for (const auto& item : msg) {
            json resp = handle_single_raw(item.dump());
            if (!resp.is_null()) {
                responses.push_back(std::move(resp));
            }
        }
        return responses.dump();
    }

    // Handle single request
    json resp = handle_single_raw(raw_message);
    return resp.is_null() ? "" : resp.dump();
}

json JsonRpc::handle_single_raw(const std::string& raw_message) {
    auto req = parse_request(raw_message);
    if (!req) {
        json err;
        err["jsonrpc"] = "2.0";
        err["id"] = nullptr;
        err["error"] = {{"code", JsonRpcErrorCode::InvalidRequest},
                         {"message", "Invalid request"}};
        return err;
    }

    // Notification: no id → no response
    if (req->id.empty()) {
        auto it = handlers_.find(req->method);
        if (it != handlers_.end()) {
            try { it->second(req->params); } catch (...) {}
        }
        return nullptr;
    }

    auto it = handlers_.find(req->method);
    if (it == handlers_.end()) {
        json err;
        err["jsonrpc"] = "2.0";
        err["id"] = req->id;
        err["error"] = {{"code", JsonRpcErrorCode::MethodNotFound},
                         {"message", "Method not found: " + req->method}};
        return err;
    }

    try {
        json result = it->second(req->params);
        json resp;
        resp["jsonrpc"] = "2.0";
        resp["id"] = req->id;
        resp["result"] = std::move(result);
        return resp;
    } catch (const std::exception& e) {
        json err;
        err["jsonrpc"] = "2.0";
        err["id"] = req->id;
        err["error"] = {{"code", JsonRpcErrorCode::InternalError},
                         {"message", std::string(e.what())}};
        return err;
    }
}

std::string JsonRpc::build_request(const std::string& id, const std::string& method,
                                    const json& params) {
    json req;
    req["jsonrpc"] = "2.0";
    req["id"] = id;
    req["method"] = method;
    if (!params.is_null()) {
        req["params"] = params;
    }
    return req.dump();
}

std::string JsonRpc::build_notification(const std::string& method, const json& params) {
    json req;
    req["jsonrpc"] = "2.0";
    req["method"] = method;
    if (!params.is_null()) {
        req["params"] = params;
    }
    return req.dump();
}

std::string JsonRpc::build_response(const std::string& id, const json& result) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = id;
    resp["result"] = result;
    return resp.dump();
}

std::string JsonRpc::build_error(const std::string& id, int code, const std::string& message,
                                  const json& data) {
    json resp;
    resp["jsonrpc"] = "2.0";
    resp["id"] = id;
    resp["error"] = {{"code", code}, {"message", message}};
    if (!data.is_null()) {
        resp["error"]["data"] = data;
    }
    return resp.dump();
}

std::optional<JsonRpcRequest> JsonRpc::parse_request(const std::string& raw_message) {
    try {
        json msg = json::parse(raw_message);
        JsonRpcRequest req;

        if (msg.contains("jsonrpc")) req.jsonrpc = msg["jsonrpc"];
        if (msg.contains("id")) {
            if (msg["id"].is_string()) req.id = msg["id"];
            else if (msg["id"].is_number()) req.id = std::to_string(msg["id"].get<int>());
        }
        if (msg.contains("method")) req.method = msg["method"];
        if (msg.contains("params")) req.params = msg["params"];

        if (req.method.empty()) return std::nullopt;
        return req;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<JsonRpcResponse> JsonRpc::parse_response(const std::string& raw_message) {
    try {
        json msg = json::parse(raw_message);
        JsonRpcResponse resp;

        if (msg.contains("jsonrpc")) resp.jsonrpc = msg["jsonrpc"];
        if (msg.contains("id")) {
            if (msg["id"].is_string()) resp.id = msg["id"];
            else if (msg["id"].is_number()) resp.id = std::to_string(msg["id"].get<int>());
        }
        if (msg.contains("result")) resp.result = msg["result"];
        if (msg.contains("error")) {
            JsonRpcError err;
            err.code = msg["error"]["code"];
            err.message = msg["error"]["message"];
            if (msg["error"].contains("data")) err.data = msg["error"]["data"];
            resp.error.emplace(err);
        }

        return resp;
    } catch (...) {
        return std::nullopt;
    }
}

std::string JsonRpc::generate_id() {
    return std::to_string(next_id_++);
}

size_t JsonRpc::method_count() const {
    return handlers_.size();
}

}  // namespace claude
