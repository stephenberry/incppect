/*! \file incppect.h
 *  \brief Enter description here.
 *  \author Georgi Gerganov
 */

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#include "App.h" // uWebSockets
#include "common.h"

template <bool SSL>
struct Incppect
{
   enum struct EventType : uint8_t {
      Connect,
      Disconnect,
      Custom,
   };

   using TGetter = std::function<std::string_view(const std::vector<int>& idxs)>;
   using THandler = std::function<void(int clientId, EventType etype, std::string_view)>;
   
   bool print_debug = false;

   // service parameters
   struct Parameters
   {
      int32_t portListen = 3000;
      int32_t maxPayloadLength_bytes = 256 * 1024;
      int64_t tLastRequestTimeout_ms = 3000;
      int32_t tIdleTimeout_s = 120;

      std::string httpRoot = ".";
      std::vector<std::string> resources{};

      std::string sslKey = "key.pem";
      std::string sslCert = "cert.pem";

      // todo:
      // max clients
      // max buffered amount
      // etc.
   };

   Incppect()
   {
      var("incppect.nclients", [this](const std::vector<int>&) { return view(socketData.size()); });
      var("incppect.tx_total", [this](const std::vector<int>&) { return view(txTotal_bytes); });
      var("incppect.rx_total", [this](const std::vector<int>&) { return view(rxTotal_bytes); });
      var("incppect.ip_address[%d]", [this](const std::vector<int>& idxs) {
         auto it = clientData.cbegin();
         std::advance(it, idxs[0]);
         return view(it->second.ipAddress);
      });
   }
   
   static int64_t timestamp()
   {
      return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch())
         .count();
   }

   // run the incppect service main loop in the current thread
   // blocking call
   void run(Parameters parameters)
   {
      this->parameters = parameters;
      run();
   }

   // terminate the server instance   
   void stop()
   {
      if (mainLoop != nullptr) {
         mainLoop->defer([this]() {
            for (auto [id, socket_data] : socketData) {
               if (socket_data->mainLoop) {
                  socket_data->mainLoop->defer([socket_data]() { socket_data->ws->close(); });
               }
            }
            us_listen_socket_close(0, listenSocket);
         });
      }
   }

   // set a resource. useful for serving html/js files from within the application   
   void setResource(const std::string& url, const std::string& content)
   {
      resources[url] = content;
   }

   // number of connected clients   
   int32_t nConnected() const
   {
      return socketData.size();
   }

   // run the incppect service main loop in dedicated thread
   // non-blocking call, returns the created std::thread   
   std::thread runAsync(Parameters parameters)
   {
      std::thread worker([this, parameters]() { this->run(parameters); });
      return worker;
   }

   // define variable/memory to inspect
   //
   // examples:
   //
   //   var("path0", [](auto ) { ... });
   //   var("path1[%d]", [](auto idxs) { ... idxs[0] ... });
   //   var("path2[%d].foo[%d]", [](auto idxs) { ... idxs[0], idxs[1] ... });
   //
   bool var(const std::string& path, TGetter&& getter)
   {
      pathToGetter[path] = getters.size();
      getters.emplace_back(std::move(getter));

      return true;
   }

   // shorthand for string_view from var
   template <class T>
      requires (std::is_trivially_copyable_v<std::decay_t<T>>)
   static std::string_view view(T& v)
   {
      if constexpr (std::is_same<T, std::string>::value) {
         return std::string_view{v.data(), v.size()};
      }
      return std::string_view{(char*)(&v), sizeof(v)};
   }

   template <class T>
      requires (std::is_trivially_copyable_v<std::decay_t<T>>)
   static std::string_view view(T&& v)
   {
      static thread_local T t;
      t = std::move(v);
      return std::string_view{(char*)(&t), sizeof(t)};
   }

   // get global instance
   static Incppect& getInstance()
   {
      static Incppect instance;
      return instance;
   }

   struct Request
   {
      int64_t tLastUpdated_ms = -1;
      int64_t tLastRequested_ms = -1;
      int64_t tMinUpdate_ms = 16;
      int64_t tLastRequestTimeout_ms = 3000;

      std::vector<int> idxs{};
      int32_t getterId = -1;

      std::string prevData{};
      std::string diffData{};
      std::string_view curData{};
   };

   struct ClientData
   {
      int64_t tConnected_ms = -1;

      std::array<uint8_t, 4> ipAddress{};

      std::vector<int32_t> lastRequests{};
      std::map<int32_t, Request> requests{};

      std::string curBuffer{};
      std::string prevBuffer{};
      std::string diffBuffer{};
   };

   struct PerSocketData
   {
      int32_t clientId = 0;

      uWS::Loop* mainLoop{};
      uWS::WebSocket<SSL, true>* ws{};
   };

   inline bool hasExt(std::string_view file, std::string_view ext)
   {
      if (ext.size() > file.size()) {
         return false;
      }
      return std::equal(ext.rbegin(), ext.rend(), file.rbegin());
   }

   void run()
   {
      mainLoop = uWS::Loop::get();

      {
         const char* kProtocol = SSL ? "HTTPS" : "HTTP";
         if (print_debug) {
            std::printf("[incppect] running instance. serving %s from '%s'\n", kProtocol, parameters.httpRoot.c_str());
         }
      }

      typename uWS::TemplatedApp<SSL>::WebSocketBehavior wsBehaviour;
      wsBehaviour.compression = uWS::SHARED_COMPRESSOR;
      //wsBehaviour.compression = uWS::DEDICATED_COMPRESSOR_256KB;
      wsBehaviour.maxPayloadLength = parameters.maxPayloadLength_bytes;
      wsBehaviour.idleTimeout = parameters.tIdleTimeout_s;
      wsBehaviour.open = [&](auto* ws, auto* /*req*/) {
         static int32_t uniqueId = 1;
         ++uniqueId;

         auto& cd = clientData[uniqueId];
         cd.tConnected_ms = timestamp();

         auto addressBytes = ws->getRemoteAddress();
         cd.ipAddress[0] = addressBytes[12];
         cd.ipAddress[1] = addressBytes[13];
         cd.ipAddress[2] = addressBytes[14];
         cd.ipAddress[3] = addressBytes[15];

         auto sd = static_cast<PerSocketData*>(ws->getUserData());
         sd->clientId = uniqueId;
         sd->ws = ws;
         sd->mainLoop = uWS::Loop::get();

         socketData.insert({uniqueId, sd});

         if (print_debug) {
            std::printf("[incppect] client with id = %d connected\n", sd->clientId);
         }

         if (handler) {
            handler(sd->clientId, EventType::Connect, {(const char*)cd.ipAddress.data(), cd.ipAddress.size()});
         }
      };
      wsBehaviour.message = [this](auto* ws, const std::string_view message, uWS::OpCode /*opCode*/) {
         rxTotal_bytes += message.size();
         if (message.size() < sizeof(int)) {
            return;
         }

         uint32_t type{};
         std::memcpy(&type, message.data(), sizeof(type));

         bool doUpdate = true;

         auto sd = static_cast<PerSocketData*>(ws->getUserData());
         auto& cd = clientData[sd->clientId];

         switch (type) {
         case 1: {
            // Custom space delimited format parsing
            // TODO: replace with BEVE
            std::stringstream ss(message.data() + 4);
            while (true) {
               Request request;

               std::string path;
               ss >> path;
               if (ss.eof()) break;
               int requestId = 0;
               ss >> requestId;
               int nidxs = 0;
               ss >> nidxs;
               for (int i = 0; i < nidxs; ++i) {
                  int idx = 0;
                  ss >> idx;
                  if (idx == -1) idx = sd->clientId;
                  request.idxs.push_back(idx);
               }

               if (const auto it = pathToGetter.find(path); it != pathToGetter.end()) {
                  if (print_debug) {
                     std::printf("[incppect] requestId = %d, path = '%s', nidxs = %d\n", requestId, path.c_str(), nidxs);
                  }
                  request.getterId = it->second;

                  cd.requests[requestId] = std::move(request);
               }
               else {
                  if (print_debug) {
                     std::printf("[incppect] missing path '%s'\n", path.c_str());
                  }
               }
            }
         } break;
         case 2: {
            const auto nRequests = (message.size() - sizeof(int32_t)) / sizeof(int32_t);
            if (nRequests * sizeof(int32_t) + sizeof(int32_t) != message.size()) {
               if (print_debug) {
                  std::printf("[incppect] error : invalid message data!\n");
               }
               return;
            }
            if (print_debug) {
               std::printf("[incppect] received requests: %d\n", int(nRequests));
            }

            cd.lastRequests.clear();
            for (size_t i = 0; i < nRequests; ++i) {
               int32_t curRequest = -1;
               std::memcpy(&curRequest, message.data() + 4 * (i + 1), sizeof(curRequest));
               if (cd.requests.find(curRequest) != cd.requests.end()) {
                  cd.lastRequests.push_back(curRequest);
                  cd.requests[curRequest].tLastRequested_ms = timestamp();
                  cd.requests[curRequest].tLastRequestTimeout_ms = parameters.tLastRequestTimeout_ms;
               }
            }
         } break;
         case 3: {
            // update time stamps
            for (auto curRequest : cd.lastRequests) {
               if (cd.requests.find(curRequest) != cd.requests.end()) {
                  cd.requests[curRequest].tLastRequested_ms = timestamp();
                  cd.requests[curRequest].tLastRequestTimeout_ms = parameters.tLastRequestTimeout_ms;
               }
            }
         } break;
         case 4: {
            // Custom event
            doUpdate = false;
            if (handler && message.size() > sizeof(int32_t)) {
               handler(sd->clientId, EventType::Custom, {message.data() + sizeof(int32_t), message.size() - sizeof(int32_t)});
            }
         } break;
         default:
               if (print_debug) {
                  std::printf("[incppect] unknown message type: %d\n", type);
               }
         };

         if (doUpdate) {
            sd->mainLoop->defer([this]() { update(); });
         }
      };
      wsBehaviour.drain = [this](auto* ws) {
         /* Check getBufferedAmount here */
         if (print_debug && ws->getBufferedAmount() > 0) {
            std::printf("[incppect] drain: buffered amount = %d\n", ws->getBufferedAmount());
         }
      };
      wsBehaviour.ping = [](auto* /*ws*/) {

      };
      wsBehaviour.pong = [](auto* /*ws*/) {

      };
      wsBehaviour.close = [this](auto* ws, int /*code*/, std::string_view /*message*/) {
         auto sd = static_cast<PerSocketData*>(ws->getUserData());
         if (print_debug) {
            std::printf("[incppect] client with id = %d disconnected\n", sd->clientId);
         }

         clientData.erase(sd->clientId);
         socketData.erase(sd->clientId);

         if (handler) {
            handler(sd->clientId, EventType::Disconnect, {nullptr, 0});
         }
      };

      std::unique_ptr<uWS::TemplatedApp<SSL>> app;

      if constexpr (SSL) {
         us_socket_context_options_t ssl_options = {};

         ssl_options.key_file_name = parameters.sslKey.data();
         ssl_options.cert_file_name = parameters.sslCert.data();

         app.reset(new uWS::TemplatedApp<SSL>(ssl_options));
      }
      else {
         app.reset(new uWS::TemplatedApp<SSL>());
      }

      if (app->constructorFailed()) {
         if (print_debug) {
            std::printf("[incppect] failed to construct uWS server!\n");
            if (SSL) {
               std::printf("[incppect] verify that you have valid certificate files:\n");
               std::printf("[incppect] key  file : '%s'\n", parameters.sslKey.c_str());
               std::printf("[incppect] cert file : '%s'\n", parameters.sslCert.c_str());
            }
         }

         return;
      }

      (*app)
         .template ws<PerSocketData>("/incppect", std::move(wsBehaviour))
         .get("/incppect.js", [](auto* res, auto* /*req*/) { res->end(kIncppect_js); });
      for (const auto& resource : parameters.resources) {
         (*app).get("/" + resource, [this](auto* res, auto* req) {
            std::string url = std::string(req->getUrl());
            std::printf("url = '%s'\n", url.c_str());

            if (url.empty()) {
               res->end("Resource not found");
               return;
            }

            if (url.back() == '/') {
               url += "index.html";
            }

            if (const auto it = resources.find(url); it != resources.end()) {
               res->end(it->second);
               return;
            }

            std::printf("resource = '%s'\n", (parameters.httpRoot + url).c_str());
            std::ifstream file(parameters.httpRoot + url);

            if (file.is_open() == false || file.good() == false) {
               res->end("Resource not found");
               return;
            }

            // TODO: optimize
            const std::string str((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

            if (str.empty()) {
               res->end("Resource not found");
               return;
            }

            if (hasExt(req->getUrl(), ".js")) {
               res->writeHeader("Content-Type", "text/javascript");
            }

            res->end(str);
         });
      }
      (*app).get("/*", [this](auto* res, auto* req) {
         const std::string_view url{req->getUrl()};
         std::printf("url = '%.*s'\n", int(url.size()), url.data());

         res->end("Resource not found");
         return;
      });
      (*app)
         .listen(parameters.portListen,
                 [this](auto* token) {
                    this->listenSocket = token;
                    if (token) {
                       std::printf("[incppect] listening on port %d\n", parameters.portListen);

                       const char* kProtocol = SSL ? "https" : "http";
                       std::printf("[incppect] %s://localhost:%d/\n", kProtocol, parameters.portListen);
                    }
                 })
         .run();
   }

   void update()
   {
      for (auto& [clientId, cd] : clientData) {
         if (socketData[clientId]->ws->getBufferedAmount()) {
            std::printf(
               "[incppect] warning: buffered amount = %d, not sending updates to client %d. waiting for buffer to "
               "drain\n",
               socketData[clientId]->ws->getBufferedAmount(), clientId);
            continue;
         }

         auto& curBuffer = cd.curBuffer;
         auto& prevBuffer = cd.prevBuffer;
         auto& diffBuffer = cd.diffBuffer;

         uint32_t typeAll = 0;
         curBuffer.resize(sizeof(typeAll));
         std::memcpy(curBuffer.data(), &typeAll, sizeof(typeAll));

         for (auto& [requestId, req] : cd.requests) {
            auto& getter = getters[req.getterId];
            auto tCur = timestamp();
            if (((req.tLastRequestTimeout_ms < 0 && req.tLastRequested_ms > 0) ||
                 (tCur - req.tLastRequested_ms < req.tLastRequestTimeout_ms)) &&
                tCur - req.tLastUpdated_ms > req.tMinUpdate_ms) {
               if (req.tLastRequestTimeout_ms < 0) {
                  req.tLastRequested_ms = 0; // resetting last requested time
               }

               req.curData = getter(req.idxs);
               req.tLastUpdated_ms = tCur;

               constexpr uint32_t kPadding = 4;

               uint32_t dataSize_bytes = uint32_t(req.curData.size());
               int padding_bytes = 0;
               {
                  int r = dataSize_bytes % kPadding;
                  while (r > 0 && r < kPadding) {
                     ++dataSize_bytes;
                     ++padding_bytes;
                     ++r;
                  }
               }

               int32_t type = 0; // full update
               if ((req.prevData.size() == (req.curData.size() + padding_bytes)) && (req.curData.size() > 256)) {
                  type = 1; // run-length encoding of diff
               }

               curBuffer.append((char*)(&requestId), sizeof(requestId));
               curBuffer.append((char*)(&type), sizeof(type));

               if (type == 0) {
                  curBuffer.append((char*)(&dataSize_bytes), sizeof(dataSize_bytes));
                  curBuffer.append(req.curData.begin(), req.curData.end());
                  curBuffer.append(padding_bytes, 0);
               }
               else if (type == 1) {
                  uint32_t a = 0;
                  uint32_t b = 0;
                  uint32_t c = 0;
                  uint32_t n = 0;
                  req.diffData.clear();

                  constexpr auto chunk_size = sizeof(uint32_t);
                  for (size_t i = 0; i < req.curData.size(); i += chunk_size) {
                     std::memcpy(&a, req.prevData.data() + i, chunk_size);
                     std::memcpy(&b, req.curData.data() + i, chunk_size);
                     a ^= b;
                     if (a == c) {
                        ++n;
                     }
                     else {
                        if (n > 0) {
                           req.diffData.append((char*)(&n), sizeof(n));
                           req.diffData.append((char*)(&c), sizeof(c));
                        }
                        n = 1;
                        c = a;
                     }
                  }

                  // Handling last few bytes, three or less
                  if (req.curData.size() % 4 != 0) {
                     a = 0;
                     b = 0;
                     const uint32_t i = uint32_t(req.curData.size()); // (req.curData.size() / 4) * 4
                     const uint32_t k = uint32_t(req.curData.size() - i);
                     std::memcpy(&a, req.prevData.data() + i, k);
                     std::memcpy(&b, req.curData.data() + i, k);
                     a ^= b;
                     if (a == c) {
                        ++n;
                     }
                     else {
                        req.diffData.append((char*)(&n), sizeof(n));
                        req.diffData.append((char*)(&c), sizeof(c));
                        n = 1;
                        c = a;
                     }
                  }

                  req.diffData.append((char*)(&n), sizeof(n));
                  req.diffData.append((char*)(&c), sizeof(c));

                  dataSize_bytes = uint32_t(req.diffData.size());
                  curBuffer.append((char*)(&dataSize_bytes), sizeof(dataSize_bytes));
                  curBuffer.append(req.diffData.begin(), req.diffData.end());
               }
               
               req.prevData = req.curData;
            }
         }

         if (curBuffer.size() > 4) {
            if (curBuffer.size() == prevBuffer.size() && curBuffer.size() > 256) {
               uint32_t a = 0;
               uint32_t b = 0;
               uint32_t c = 0;
               uint32_t n = 0;
               diffBuffer.clear();

               uint32_t typeAll = 1;
               diffBuffer.append((char*)(&typeAll), sizeof(typeAll));

               for (int i = 4; i < (int)curBuffer.size(); i += 4) {
                  std::memcpy(&a, prevBuffer.data() + i, sizeof(uint32_t));
                  std::memcpy(&b, curBuffer.data() + i, sizeof(uint32_t));
                  a ^= b;
                  if (a == c) {
                     ++n;
                  }
                  else {
                     if (n > 0) {
                        diffBuffer.append((char*)(&n), sizeof(n));
                        diffBuffer.append((char*)(&c), sizeof(c));
                     }
                     n = 1;
                     c = a;
                  }
               }

               diffBuffer.append((char*)(&n), sizeof(n));
               diffBuffer.append((char*)(&c), sizeof(c));

               if (int32_t(diffBuffer.size()) > parameters.maxPayloadLength_bytes) {
                  std::printf("[incppect] warning: buffer size (%d) exceeds maxPayloadLength (%d)\n",
                            int(diffBuffer.size()), parameters.maxPayloadLength_bytes);
               }

               // compress only for message larger than 64 bytes
               const bool doCompress = diffBuffer.size() > 64;

               if (socketData[clientId]->ws->send({diffBuffer.data(), diffBuffer.size()}, uWS::OpCode::BINARY,
                                                  doCompress) == false) {
                  std::printf("[incpeect] warning: backpressure for client %d increased \n", clientId);
               }
            }
            else {
               if (int32_t(curBuffer.size()) > parameters.maxPayloadLength_bytes) {
                  std::printf("[incppect] warning: buffer size (%d) exceeds maxPayloadLength (%d)\n",
                            int(curBuffer.size()), parameters.maxPayloadLength_bytes);
               }

               // compress only for message larger than 64 bytes
               const bool doCompress = curBuffer.size() > 64;

               if (socketData[clientId]->ws->send({curBuffer.data(), curBuffer.size()}, uWS::OpCode::BINARY,
                                                  doCompress) == false) {
                  std::printf("[incpeect] warning: backpressure for client %d increased \n", clientId);
               }
            }

            txTotal_bytes += curBuffer.size();
            
            prevBuffer = curBuffer;
         }
      }
   }

   Parameters parameters;

   double txTotal_bytes = 0.0;
   double rxTotal_bytes = 0.0;

   std::unordered_map<std::string, int> pathToGetter;
   std::vector<TGetter> getters;

   uWS::Loop* mainLoop = nullptr;
   us_listen_socket_t* listenSocket = nullptr;
   std::map<int, PerSocketData*> socketData;
   std::map<int, ClientData> clientData;

   std::map<std::string, std::string> resources;

   THandler handler{}; // handle input from the clients
};
