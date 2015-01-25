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
  kIgnore = 3
};

Client::Client(const std::string& port, const std::string& torrent) {
  nPort = port;

  // Generate a randomized peer_id
  nPeerId = generatePeer();

  nInfo = new MetaInfo();
  nHttpResponse = new HttpResponse();
  nTrackerResponse = new TrackerResponse();

  // Read the torrent file into a filestream and decode
  ifstream torrentStream(torrent, ifstream::in);
  nInfo->wireDecode(torrentStream);

  // Extract the tracker_url and tracker_port from the announce
  extract(nInfo->getAnnounce(), nTrackerUrl, nTrackerPort, nTrackerEndpoint);


  // Connect to the tracker
  connectTracker();
}

Client::~Client() {
  delete nHttpResponse;
  delete nTrackerResponse;
  delete nInfo;
}

/*
 * Client connects to the tracker and sends the GET request to the tracker
 * Since we are using HTTP/1.0, we need to, for every request
 * - initialize the socket
 * - establish the connection
 * - send the message
 * - receive/parse/decode the response
 * - close the connection
 */
int Client::connectTracker() {

  getRequest = prepareRequest(kStarted);
  int num_times = 0;

  while (true) {
    // Create socket using TCP IP
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // Connect to server using tracker's port
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(atoi(nTrackerPort.c_str()));
    serverAddr.sin_addr.s_addr = inet_addr(TRACKER_IP);
    memset(serverAddr.sin_zero, '\0', sizeof(serverAddr.sin_zero));

    // Connect to the server
    if (connect(sockfd, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) == -1) {
      fprintf(stderr, "Failed to connect to tracker at port: %d\n", ntohs(serverAddr.sin_port));
      return RC_TRACKER_CONNECTION_FAILED;
    }

    if (nTrackerResponse != NULL) {
    }
    // Send GET request to the tracker
    if (send(sockfd, getRequest.c_str(), getRequest.size(), 0) == -1) {
      fprintf(stderr, "Failed to send GET request to tracker at port: %d\n", ntohs(serverAddr.sin_port));
      return RC_SEND_GET_REQUEST_FAILED;
    }

    char buf[1000] = {'\0'};
    if (recv(sockfd, buf, sizeof(buf), 0) == -1) {
      fprintf(stderr, "Failed to receive a response from tracker.\n");
      return RC_NO_TRACKER_RESPONSE;
    }

    int buf_size = 0;
    for(; buf[buf_size] != '\0'; buf_size++);

    if (buf_size > 0) {
      const char* res_body;
      res_body = nHttpResponse->parseResponse(buf, buf_size);
      istringstream responseStream(res_body);

      bencoding::Dictionary dict;
      dict.wireDecode(responseStream);
      nTrackerResponse->decode(dict);

      if (nTrackerResponse->isFailure()) {
        fprintf(stderr, "Fail:%s\n", nTrackerResponse->getFailure().c_str());
        return RC_TRACKER_RESPONSE_FAILED;
      }

      if (num_times == 0) {
        std::vector<PeerInfo> peers = nTrackerResponse->getPeers();
        std::vector<PeerInfo>::iterator it = peers.begin();
        for (; it != peers.end(); it++) {
          cout << it->ip << ":" << it->port << endl;
        }
      }

      getRequest = prepareRequest(kIgnore);
    }

    sleep(nTrackerResponse->getInterval());

    num_times++;

    close(sockfd);
  }

  return 0;
}

/*
 * Formats and prepares a GET request to the tracker's announce url.
 * Takes in an event type and returns the prepared request.
 */
string Client::prepareRequest(int event) {
  string url_f = "/%s?info_hash=%s&peer_id=%s&port=%s&uploaded=0&downloaded=0&left=%d";
  const char* url_hash = url::encode((const uint8_t *)(nInfo->getHash()->get()), 20).c_str();
  const char* url_id = url::encode((const uint8_t *)nPeerId.c_str(), 20).c_str();

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
    nTrackerEndpoint.c_str(),
    url_hash,
    url_id,
    nPort.c_str(),
    url_left
  );
  string request = request_url;

  // Set up a HttpRequest and fill its parameters
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
int Client::extract(const string& url, string& domain, string& port, string& endpoint) {
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
        endpoint = url.substr(third + 1, url.size());
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
