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
#include <sstream>

#include "common.hpp"
#include "meta-info.hpp"
#include "http/url-encoding.hpp"

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
    nInfo = new MetaInfo();
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

    fprintf(stdout, "%s", request_url);
    return request;
  }

private:
  MetaInfo* nInfo;
  string nPort;
  string nPeerId;
};

} // namespace sbt

#endif // SBT_CLIENT_HPP
