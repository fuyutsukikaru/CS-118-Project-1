/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (c) 2014,  Regents of the University of California
 *
 * This file is part of Simple BT.
 * See AUTHORS.md for complete list of Simple BT authors and contributors.
 *
 * NSL is free software: you can redistribute it and/or modify it under the terms
 * of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * NSL is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * NSL, e.g., in COPYING.md file.  If not, see <http://www.gnu.org/licenses/>.
 *
 * \author Yingdi Yu <yingdi@cs.ucla.edu>
 */

#ifndef SBT_CLIENT_HPP
#define SBT_CLIENT_HPP

#define BUFFER_SIZE 4096

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

#include <iostream>
#include <fstream>

#include "common.hpp"
#include "meta-info.hpp"
#include "http/url-encoding.hpp"
#include "http/http-request.hpp"

using namespace std;

namespace sbt {

enum eventTypes : int {
  kStarted = 0,
  kCompleted = 1,
  kStopped = 2,
};

class Client
{
public:
  Client(const std::string& port, const std::string& torrent)
  {
    nPort = port;
    createConnection();

    nInfo = new MetaInfo();
    ifstream torrentStream(torrent, ifstream::in);
    nInfo->wireDecode(torrentStream);
    string temp = prepareRequest(0);
    getRequest = new char[temp.length() + 1];
    strcpy(getRequest, temp.c_str());

    connectTracker();
  }

  int createConnection() {
    // create socket using TCP IP
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in clientAddr;
    clientAddr.sin_family = AF_INET;
    clientAddr.sin_port = htons(atoi(nPort.c_str()));
    clientAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (bind(sockfd, (struct sockaddr*) &clientAddr, sizeof(clientAddr)) == -1) {
      fprintf(stderr, "Failed to connect client to port: %s\n", nPort.c_str());
      return 3;
    }

    return 0;
  }

  int connectTracker() {
    // Connect to server using tracker's port
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(trackerPort);
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1");
    memset(serverAddr.sin_zero, '\0', sizeof(serverAddr.sin_zero));

    // connect to the server
    if (connect(sockfd, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) == -1) {
      fprintf(stderr, "Failed to connect to tracker at port: %d\n", ntohs(serverAddr.sin_port));
      return 2;
    }

    // send GET request to the tracker
    if (send(sockfd, getRequest, sizeof(getRequest), 0) == -1) {
      fprintf(stderr, "Failed to send GET request to tracker at port: %d\n", ntohs(serverAddr.sin_port));
      return 4;
    }

    char buf[100] = {'\0'};
    if (recv(sockfd, buf, 100, 0) != -1) {
      fprintf(stdout, "Received the response!");
    }

    return 0;
  }

  string prepareRequest(int event) {
    string url_f = "/announce?info_hash=%s&peer_id=%s&port=%s&uploaded=0&downloaded=0&left=%d";

    const char* url_hash = (url::encode((const uint8_t *)(nInfo->getHash()).get(), 20)).c_str();
    const char* url_peer_id = (url::encode((const uint8_t *)nPeerId.c_str(), 20)).c_str();
    int url_left = nInfo->getLength();

    string url_event = "";
    switch(event) {
      case kStarted:
        url_event = "&event=started";
        break;
      case kCompleted:
        url_event = "&event=completed";
        break;
      case kStopped:
        url_event = "&event=stopped";
        break;
    }
    url_f += url_event;

    const char* url_f_c = url_f.c_str();
    char request_url[BUFFER_SIZE];
    sprintf(request_url, url_f_c, url_hash, url_peer_id, nPort.c_str(), url_left);
    string request = request_url;

    HttpRequest req;
    req.setHost(nTrackerUrl);
    req.setPort(atoi(nPort.c_str()));
    req.setMethod(HttpRequest::GET);
    req.setVersion("1.0");
    req.setPath(request);
    req.addHeader("Accept-Language", "en-US");

    size_t req_length = req.getTotalLength();
    char *buf = new char[req_length];
    req.formatRequest(buf);

    request = buf;
    return request;
  }

private:
  MetaInfo* nInfo;
  unsigned short trackerPort;
  int sockfd;
  string nPort;
  string nPeerId;
  string nTrackerUrl;
  char* getRequest;
};

} // namespace sbt

#endif // SBT_CLIENT_HPP
