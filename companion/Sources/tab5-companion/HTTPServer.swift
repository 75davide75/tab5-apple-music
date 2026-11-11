import Foundation
import Network

struct HTTPRequest {
    let method: String
    let path: String
    let query: [String: String]
    let body: Data
}

struct HTTPResponse {
    var status = 200
    var contentType = "application/json"
    var body = Data()

    static func notFound() -> HTTPResponse { HTTPResponse(status: 404, body: Data("{}".utf8)) }
}

/// A very small HTTP/1.1 server (one request per connection), announced over Bonjour.
final class HTTPServer {
    private let listener: NWListener
    private let handler: (HTTPRequest) -> HTTPResponse
    private let work = DispatchQueue(label: "http-work", attributes: .concurrent)

    init(port: UInt16, service: String, handler: @escaping (HTTPRequest) -> HTTPResponse) throws {
        self.handler = handler
        let params = NWParameters.tcp
        params.allowLocalEndpointReuse = true
        listener = try NWListener(using: params, on: NWEndpoint.Port(rawValue: port)!)
        listener.service = NWListener.Service(name: "Tab5 Music Companion", type: service)
    }

    func start() {
        listener.newConnectionHandler = { [weak self] connection in
            connection.start(queue: .global())
            self?.receive(connection, buffer: Data())
        }
        listener.stateUpdateHandler = { state in
            if case .failed(let error) = state { log("listener failed: \(error)") }
        }
        listener.start(queue: .global())
    }

    private func receive(_ connection: NWConnection, buffer: Data) {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] data, _, isComplete, error in
            guard let self else { return }
            var buffer = buffer
            if let data { buffer.append(data) }
            if let request = Self.parse(buffer) {
                self.work.async {
                    let response = self.handler(request)
                    self.send(response, on: connection)
                }
            } else if isComplete || error != nil || buffer.count > 1_000_000 {
                connection.cancel()
            } else {
                self.receive(connection, buffer: buffer)
            }
        }
    }

    /// Returns nil until the whole request (headers and declared body) has arrived.
    static func parse(_ buffer: Data) -> HTTPRequest? {
        guard let headerEnd = buffer.range(of: Data("\r\n\r\n".utf8)),
              let head = String(data: buffer[..<headerEnd.lowerBound], encoding: .utf8) else { return nil }
        let lines = head.components(separatedBy: "\r\n")
        let first = lines[0].split(separator: " ")
        guard first.count >= 2 else { return nil }
        var length = 0
        for line in lines.dropFirst() where line.lowercased().hasPrefix("content-length:") {
            length = Int(line.dropFirst(15).trimmingCharacters(in: .whitespaces)) ?? 0
        }
        let bodyStart = headerEnd.upperBound
        guard buffer.count - bodyStart >= length else { return nil }
        let target = URLComponents(string: String(first[1]))
        var query: [String: String] = [:]
        for item in target?.queryItems ?? [] { query[item.name] = item.value ?? "" }
        return HTTPRequest(method: String(first[0]), path: target?.path ?? "/", query: query,
                           body: buffer.subdata(in: bodyStart..<(bodyStart + length)))
    }

    private func send(_ response: HTTPResponse, on connection: NWConnection) {
        let reason = response.status == 200 ? "OK" : (response.status == 204 ? "No Content" : "Error")
        var out = Data("""
        HTTP/1.1 \(response.status) \(reason)\r
        Content-Type: \(response.contentType)\r
        Content-Length: \(response.body.count)\r
        Connection: close\r
        \r\n
        """.utf8)
        out.append(response.body)
        connection.send(content: out, completion: .contentProcessed { _ in connection.cancel() })
    }
}
