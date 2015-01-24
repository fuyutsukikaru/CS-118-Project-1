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

#include "common.hpp"

namespace sbt {

class Client
{
public:
  Client(const std::string& port, const std::string& torrent)
  {
    nInfo = new MetaInfo();
    istream torrentStream = std::istringstream is(torrent);
    nInfo.wireDecode(torrentStream);

    char* getRequest = prepareRequest().c_str();

    nRequest = new HttpRequest();
    nRequest->parseRequest(getRequest);
    trackerPort = nRequest->getPort();    
  }

  int createConnection(const std::string& port) {
    // create socket using TCP IP
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr_in clientAddr;
    serverAddr.
    socklen_t clientAddrLen = sizeof(clientAddr);
    if (getsockname(sockfd, (struct sockaddr*) &clientAddr, &clientAddrLen) == -1) {
      perror("getsockname");
      return 3;
    }

    // connect client
    char ipstr[INET_ADDRSTRLEN] = {'/0'};
    inet_ntop(clientAddr.sin_family, &clientAddr.sin_addr, ipstr, sizeof(ipstr));
    cout << "Set up a connection from: " << ipstr << ":" << ntohs(clientAddr.sin_port) << endl;
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
      perror("connect");
      return 2;
    }

    // Write GET request to the server
  }

private:
  MetaInfo* nInfo;
  HttpRequest* nRequest;
  unsigned short trackerPort;
  int sockfd;
};

} // namespace sbt

#endif // SBT_CLIENT_HPP
