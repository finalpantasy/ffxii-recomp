// Mod lab page server and command line (mod_lab.h).
//
// A small HTTP/1.0 server on 127.0.0.1 only: GET / serves the page,
// GET /api/list the lab mods, GET /api/run?iters=N&reps=R&only=k1,k2 runs
// the lab. One request at a time; a run blocks the server until it ends.
// Requests whose Host header is not this address are refused, so other
// web pages cannot drive it through DNS rebinding.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <shellapi.h>
#include <windows.h>
using Sock = SOCKET;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Sock = int;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
#endif

#include "mod_lab.h"

namespace ffxii::modlab {
namespace {

const char kPage[] =
#include "mod_lab_page.inc"
    ;

std::string QueryValue(const std::string& query, const std::string& key) {
  size_t p = 0;
  while (p <= query.size()) {
    const size_t amp = query.find('&', p);
    const std::string part = query.substr(p, amp == std::string::npos ? std::string::npos : amp - p);
    const size_t eq = part.find('=');
    if (part.substr(0, eq) == key) {
      std::string v = eq == std::string::npos ? "" : part.substr(eq + 1), out;
      for (size_t i = 0; i < v.size(); i++) {
        if (v[i] == '%' && i + 2 < v.size()) {
          out += char(std::strtol(v.substr(i + 1, 2).c_str(), nullptr, 16));
          i += 2;
        } else {
          out += v[i] == '+' ? ' ' : v[i];
        }
      }
      return out;
    }
    if (amp == std::string::npos) break;
    p = amp + 1;
  }
  return "";
}

std::vector<std::string> SplitComma(const std::string& s) {
  std::vector<std::string> out;
  size_t p = 0;
  while (p < s.size()) {
    size_t c = s.find(',', p);
    if (c == std::string::npos) c = s.size();
    if (c > p) out.push_back(s.substr(p, c - p));
    p = c + 1;
  }
  return out;
}

void Send(Sock s, int code, const char* type, const std::string& body) {
  const char* status = code == 200 ? "OK" : code == 404 ? "Not Found" : "Forbidden";
  std::string head = "HTTP/1.0 " + std::to_string(code) + " " + status + "\r\nContent-Type: " + type +
                     "\r\nContent-Length: " + std::to_string(body.size()) +
                     "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
  head += body;
  size_t sent = 0;
  while (sent < head.size()) {
    const int n = send(s, head.data() + sent, int(head.size() - sent), 0);
    if (n <= 0) break;
    sent += size_t(n);
  }
}

}  // namespace

int Serve(const std::string& dir, int port, bool open_browser) {
#if defined(_WIN32)
  WSADATA wsa;
  if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
    std::cerr << "mod lab: WSAStartup failed\n";
    return 1;
  }
#endif
  Sock ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (ls == INVALID_SOCKET) {
    std::cerr << "mod lab: socket failed\n";
    return 1;
  }
#if defined(_WIN32)
  BOOL excl = TRUE;
  setsockopt(ls, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&excl), sizeof excl);
#endif
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(uint16_t(port));
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(ls, 8) != 0) {
    std::cerr << "mod lab: cannot listen on 127.0.0.1:" << port << " (in use? try --mod-lab-port)\n";
    CLOSESOCK(ls);
    return 1;
  }
  const std::string host = "127.0.0.1:" + std::to_string(port);
  const std::string url = "http://" + host + "/";
  std::cout << "Mod Lab: " << url << "  (lab mods: " << dir << ")\n" << std::flush;
#if defined(_WIN32)
  if (open_browser) ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
  (void)open_browser;
#endif
  for (;;) {
    Sock c = accept(ls, nullptr, nullptr);
    if (c == INVALID_SOCKET) continue;
    std::string req;
    char buf[4096];
    while (req.find("\r\n\r\n") == std::string::npos && req.size() < 65536) {
      const int n = recv(c, buf, sizeof buf, 0);
      if (n <= 0) break;
      req.append(buf, size_t(n));
    }
    const size_t sp1 = req.find(' '), sp2 = req.find(' ', sp1 + 1);
    const std::string method = req.substr(0, sp1);
    const std::string target = sp1 == std::string::npos ? "" : req.substr(sp1 + 1, sp2 - sp1 - 1);
    const size_t q = target.find('?');
    const std::string path = target.substr(0, q);
    const std::string query = q == std::string::npos ? "" : target.substr(q + 1);
    std::string host_hdr;
    for (size_t p = req.find("\r\n"); p != std::string::npos; p = req.find("\r\n", p + 2)) {
      const size_t e = req.find("\r\n", p + 2);
      const std::string line = req.substr(p + 2, e == std::string::npos ? std::string::npos : e - p - 2);
      if (line.size() > 5 && (line.compare(0, 5, "Host:") == 0 || line.compare(0, 5, "host:") == 0)) {
        host_hdr = line.substr(5);
        while (!host_hdr.empty() && host_hdr.front() == ' ') host_hdr.erase(0, 1);
      }
    }
    if (method != "GET" || (host_hdr != host && host_hdr != "localhost:" + std::to_string(port))) {
      Send(c, 403, "text/plain", "forbidden\n");
    } else if (path == "/") {
      Send(c, 200, "text/html; charset=utf-8", kPage);
    } else if (path == "/api/list") {
      Send(c, 200, "application/json", ListJson(dir));
    } else if (path == "/api/run") {
      RunOptions opt;
      if (auto v = QueryValue(query, "iters"); !v.empty())
        opt.iters = std::max<uint64_t>(1000, std::min<uint64_t>(std::strtoull(v.c_str(), nullptr, 10), 50000000));
      if (auto v = QueryValue(query, "reps"); !v.empty()) opt.reps = std::max(1, std::min(std::atoi(v.c_str()), 25));
      opt.only = SplitComma(QueryValue(query, "only"));
      std::cout << "Mod Lab: run, " << opt.iters << " calls x " << opt.reps << "\n" << std::flush;
      Send(c, 200, "application/json", RunJson(dir, opt));
    } else {
      Send(c, 404, "text/plain", "not found\n");
    }
    CLOSESOCK(c);
  }
}

int Main(int argc, char** argv, const std::string& exe_dir) {
  bool lab = false, run = false, browser = true;
  int port = 8723;
  std::string dir = exe_dir + "/modlab";
  RunOptions opt;
  for (int i = 1; i < argc; i++) {
    const std::string a = argv[i];
    if (a == "--mod-lab") lab = true;
    else if (a == "--mod-lab-run") run = true;
    else if (a == "--no-browser") browser = false;
    else if (a == "--mod-lab-dir" && i + 1 < argc) dir = argv[++i];
    else if (a == "--mod-lab-port" && i + 1 < argc) port = std::atoi(argv[++i]);
    else if (a == "--mod-lab-iters" && i + 1 < argc) opt.iters = std::strtoull(argv[++i], nullptr, 10);
    else if (a == "--mod-lab-reps" && i + 1 < argc) opt.reps = std::atoi(argv[++i]);
    else if (a == "--mod-lab-only" && i + 1 < argc) opt.only = SplitComma(argv[++i]);
  }
  if (!lab && !run) return -1;
  if (run) {
    std::cout << RunJson(dir, opt) << "\n";
    return 0;
  }
  return Serve(dir, port, browser);
}

}  // namespace ffxii::modlab
