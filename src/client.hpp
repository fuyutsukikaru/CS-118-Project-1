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

#define PEER_ID_PREFIX "-CC0001-"

using namespace std;

namespace sbt {

class Client
{
public:
  Client(const string& port, const string& torrent);

  int createConnection();
  int connectTracker();
  string prepareRequest(int event);

private:
  int extract(const string& url, string& domain, string& port);
  string generatePeer();

  MetaInfo* nInfo;
  int sockfd;
  string nPort;
  string nPeerId;
  string nTrackerUrl;
  string nTrackerPort;
  string getRequest;
};

} // namespace sbt

#endif // SBT_CLIENT_HPP
