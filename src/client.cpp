/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (C) 2015 by Codifica
 * Redistribution of this file is permitted under the terms of the GNU
 * Public License (GPL).
 *
 * @date 1/24/2015
 * @author James Wu <wuzhonglin@ucla.edu>
 */

#include "client.hpp"

using namespace std;

namespace sbt {

enum eventTypes : int {
  kStarted = 0,
  kCompleted = 1,
  kStopped = 2,
};

Client::Client(const std::string& port, const std::string& torrent) {
  // create socket using TCP IP
  sockfd = socket(AF_INET, SOCK_STREAM, 0);

  nPort = port;
  nPeerId = generatePeer();

  //createConnection();

  nInfo = new MetaInfo();
  ifstream torrentStream(torrent, ifstream::in);
  nInfo->wireDecode(torrentStream);
  extract(nInfo->getAnnounce(), nTrackerUrl, nTrackerPort);

  getRequest = prepareRequest(kStarted);

  connectTracker();
}

int Client::createConnection() {
  struct sockaddr_in clientAddr;
  /*clientAddr.sin_family = AF_INET;
  clientAddr.sin_port = htons(atoi(nPort.c_str()));
  clientAddr.sin_addr.s_addr = inet_addr(CLIENT_IP);
  if (bind(sockfd, (struct sockaddr*) &clientAddr, sizeof(clientAddr)) == -1) {
    fprintf(stderr, "Failed to connect client to port: %s\n", nPort.c_str());
    return RC_CLIENT_CONNECTION_FAILED;
  }*/
  socklen_t clientAddrLen = sizeof(clientAddr);
  if (getsockname(sockfd, (struct sockaddr*) &clientAddr, &clientAddrLen) == -1) {
    fprintf(stderr, "Failed to connect client.\n");
    return RC_CLIENT_CONNECTION_FAILED;
  }

  return 0;
}

int Client::connectTracker() {
  // Connect to server using tracker's port
  struct sockaddr_in serverAddr;
  serverAddr.sin_family = AF_INET;
  serverAddr.sin_port = htons(atoi(nTrackerPort.c_str()));
  serverAddr.sin_addr.s_addr = inet_addr(TRACKER_IP);
  memset(serverAddr.sin_zero, '\0', sizeof(serverAddr.sin_zero));

  // connect to the server
  if (connect(sockfd, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) == -1) {
    fprintf(stderr, "Failed to connect to tracker at port: %d\n", ntohs(serverAddr.sin_port));
    return RC_TRACKER_CONNECTION_FAILED;
  }

  //struct sockaddr_in clientAddr;
  /*clientAddr.sin_family = AF_INET;
  clientAddr.sin_port = htons(atoi(nPort.c_str()));
  clientAddr.sin_addr.s_addr = inet_addr(CLIENT_IP);
  if (bind(sockfd, (struct sockaddr*) &clientAddr, sizeof(clientAddr)) == -1) {
    fprintf(stderr, "Failed to connect client to port: %s\n", nPort.c_str());
    return RC_CLIENT_CONNECTION_FAILED;
  }
  socklen_t clientAddrLen = sizeof(clientAddr);
  if (getsockname(sockfd, (struct sockaddr*) &clientAddr, &clientAddrLen) == -1) {
    fprintf(stderr, "Failed to connect client.\n");
    return RC_CLIENT_CONNECTION_FAILED;
  }

  char ipstr[INET_ADDRSTRLEN] = {'\0'};
  inet_ntop(clientAddr.sin_family, &clientAddr.sin_addr, ipstr, sizeof(ipstr));
  std::cout << "Set up a connection from: " << ipstr << ":" <<
  ntohs(clientAddr.sin_port) << std::endl;*/

  while (true) {
    /*
    if (nTrackerResponse != NULL) {
      fprintf(stdout, "Interval %d\n", nTrackerResponse->getInterval());
      sleep(nTrackerResponse->getInterval());
      delete nTrackerResponse;
    }
    */

    // send GET request to the tracker
    if (send(sockfd, getRequest.c_str(), getRequest.size(), 0) == -1) {
      fprintf(stderr, "Failed to send GET request to tracker at port: %d\n", ntohs(serverAddr.sin_port));
      return RC_SEND_GET_REQUEST_FAILED;
    }

    char buf[1000] = {'\0'};
    if (recv(sockfd, buf, sizeof(buf), 0) != -1) {
      fprintf(stdout, "Received the response!");

      /*
      int buf_size = 0;
      for(; buf[buf_size] != '\0'; buf_size++);

      const char* res_body;
      res_body = nHttpResponse.parseResponse(buf, buf_size);
      istringstream responseStream(res_body);

      bencoding::Dictionary dict;
      dict.wireDecode(responseStream);
      nTrackerResponse = new TrackerResponse();
      nTrackerResponse->decode(dict);
      */
      break;
    }

  }

  close(sockfd);

  return 0;
}

/*
 * Formats and prepares a GET request to the tracker's announce url.
 * Takes in an event type and returns the prepared request.
 */
string Client::prepareRequest(int event) {
  string url_f = "/announce.php?info_hash=%s&peer_id=%s&port=%s&uploaded=0&downloaded=0&left=%d";

  const uint8_t *info_hash = nInfo->getHash()->get();
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
  url_left = 0;

  const char* url_f_c = url_f.c_str();
  char request_url[BUFFER_SIZE];
  sprintf(
    request_url,
    url_f_c,
    url::encode(info_hash, 20).c_str(),
    url::encode((const uint8_t *)nPeerId.c_str(), 20).c_str(),
    nPort.c_str(),
    url_left
  );
  string request = request_url;

  HttpRequest req;
  req.setHost(nTrackerUrl);
  req.setPort(atoi(nTrackerPort.c_str()));
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

/*
 * Extracts a host and port number from the given url.
 * Returns RC_INVALID_URL if parsing failed and 0 otherwise.
 */
int Client::extract(const string& url, string& domain, string& port) {
  size_t first = url.find("://");
  if (first != string::npos) {
    first += 3;
    size_t second = url.find(":", first);
    if (second != string::npos) {
      size_t third = url.find("/", second);
      if (third != string::npos) {
        domain = url.substr(first, second - first);
        second += 1;
        port = url.substr(second, third - second);

        return 0;
      }
    }
  }

  return RC_INVALID_URL;
}

/*
 * Generates a peer_id according to the Azureus-style convention, i.e.
 * two characters of client id and four digits of version number
 * surrounded by hyphens, followed by twelve random numbers.
 *
 * More info: https://wiki.theory.org/BitTorrentSpecification#peer_id
 */
string Client::generatePeer() {
  string peer_id = PEER_ID_PREFIX;
  for (int i = 0; i < 12; i++) {
    peer_id += to_string(rand() % 10);
  }

  return peer_id;
}

} // namespace sbt
