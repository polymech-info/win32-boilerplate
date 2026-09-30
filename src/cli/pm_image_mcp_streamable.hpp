#pragma once

namespace httplib {
class Server;
}

namespace media::mcp_embed {

/// Model Context Protocol **Streamable HTTP** transport (spec 2025-11-25): single path
/// `POST /mcp` + `GET /mcp` (SSE), for Cursor `"url": "http://127.0.0.1:<port>/mcp"`.
void register_mcp_streamable_http(httplib::Server& svr);

} // namespace media::mcp_embed
