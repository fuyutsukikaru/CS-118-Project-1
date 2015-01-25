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
  nPort = port;
  createConnection();

  nPeerId = "kajgelajgelkajgleajgeklajgelkajge";

  nInfo = new MetaInfo();
  ifstream torrentStream(torrent, ifstream::in);
  nInfo->wireDecode(torrentStream);
  extract(nInfo->getAnnounce(), nTrackerUrl, nTrackerPort);

  string temp = prepareRequest(kStarted);
  getRequest = new char[temp.length() + 1];
  strcpy(getRequest, temp.c_str());

  connectTracker();
}

int Client::createConnection() {
  // create socket using TCP IP
  sockfd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in clientAddr;
  clientAddr.sin_family = AF_INET;
  clientAddr.sin_port = htons(atoi(nPort.c_str()));
  clientAddr.sin_addr.s_addr = inet_addr(CLIENT_IP);
  if (bind(sockfd, (struct sockaddr*) &clientAddr, sizeof(clientAddr)) == -1) {
    fprintf(stderr, "Failed to connect client to port: %s\n", nPort.c_str());
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

  // send GET request to the tracker
  if (send(sockfd, getRequest, sizeof(getRequest), 0) == -1) {
    fprintf(stderr, "Failed to send GET request to tracker at port: %d\n", ntohs(serverAddr.sin_port));
    return RC_SEND_GET_REQUEST_FAILED;
  }

  char buf[100] = {'\0'};
  if (recv(sockfd, buf, 100, 0) != -1) {
    fprintf(stdout, "Received the response!");
  }

  return 0;
}

string Client::prepareRequest(int event) {
  string url_f = "/announce?info_hash=%s&peer_id=%s&port=%s&uploaded=0&downloaded=0&left=%d";

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

} // namespace sbt
