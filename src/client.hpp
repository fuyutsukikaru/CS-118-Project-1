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
#define PIECE_HASH  20

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <map>
#include <utility>
#include <iostream>
#include <fstream>
#include <sstream>

#include "common.hpp"
#include "meta-info.hpp"
#include "http/url-encoding.hpp"
#include "http/http-request.hpp"
#include "http/http-response.hpp"
#include "util/hash.hpp"
#include "msg/msg-base.hpp"
#include "msg/handshake.hpp"
#include "tracker-response.hpp"
#include "msg/handshake.hpp"

#define SIMPLEBT_TEST true
#define PEER_ID_PREFIX "-CC0001-"
#define TEST_PEER_ID "SIMPLEBT.TEST.PEERID"

using namespace std;

namespace sbt {

// uniquely identify peers through <ip, port>
typedef pair<string, int> pAttr;

struct Peer {
  bool unchoked = false;
  bool sentHandshake = false;
  bool sentBitfield = false;
};

enum eventTypes : int {
  kIgnore = -1,
  kStarted = 0,
  kCompleted = 1,
  kStopped = 2
};

class Client
{
public:
  Client(const string& port, const string& torrent);

  ~Client();

  int bindClient(string& clientPort, string ipaddr);
  int createConnection(string ip, string port, int &sockfd);
  int createConnection(string ip, uint16_t port, int &sockfd);
  int connectTracker();
  int prepareRequest(string& request, int event = kIgnore);
  int prepareHandshake(int &sockfd, ConstBufferPtr infoHash, PeerInfo peer);
  int sendUnchoke(pAttr peer);

private:
  int extract(const string& url, string& domain, string& port, string& endpoint);
  int resolveHost(string& url, string& ip);
  int fck();
  int fpck(int index, int length); // file piece check
  int parseMessage(int& sockfd, ConstBufferPtr msg, pAttr peer);
  string generatePeer();
  void initBitfield();

  int sockfd;
  int clientSockfd;
  int nDownloaded = 0;
  int nUploaded = 0;
  int nRemaining = 0;

  // the best function I have ever written
  int nitroConnect(int sleep_count);

  int sendPayload(int& sockfd, msg::MsgBase& payload, pAttr peer);

  // functions for sending messages
  int sendBitfield(int& sockfd, pAttr peer);
  int sendRequest(int& sockfd, pAttr peer);
  int sendInterested(int& sockfd, pAttr peer);
  int sendHave(int& sockfd, pAttr peer);

  // functions for dealing with messages
  int handleBitfield(ConstBufferPtr msg, pAttr peer);
  int handlePiece(ConstBufferPtr msg, pAttr peer);
  int handleUnchoke(ConstBufferPtr msg, pAttr peer);

  // functions for receiving messages
  int receivePayload(int& sockfd, pAttr peer);

  char getBit(char* array, int index);

  uint8_t getBit(uint8_t* array, int index);

  string nPort;
  string nPeerId;
  string nTrackerUrl;
  string nTrackerPort;
  string nTrackerEndpoint;
  string getRequest;
  uint8_t* nBitfield;
  ssize_t nFieldSize;

  vector<int> sockArray;

  // maps peer attributes to the message id of the last
  // message sent to them
  map<pAttr, msg::MsgId> lastRektMsgType;

  // maps socket to the peer that is connected to it
  map<int, PeerInfo> socketToPeer;
  //map<pAttr, int> peerToSocket;

  map<pAttr, const uint8_t*> peerBitfields;
  map<pAttr, Peer> peerStatus;

  // maps peer attributes to whether we have sent to them
  vector<pAttr> hasPeerConnected;

  MetaInfo* nInfo;
  HttpResponse* nHttpResponse;
  TrackerResponse* nTrackerResponse;
  vector<PeerInfo> peers;
  msg::HandShake* nHandshake;
};

} // namespace sbt

#endif // SBT_CLIENT_HPP
