/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/**
 * Copyright (C) 2015 by Codifica
 * Redistribution of this file is permitted under the terms of the GNU
 * Public License (GPL).
 *
 * @date 1/24/2015
 * @author Soomin Jeong <sjeongus@ucla.edu>
 * @author Jeffrey Wang <oojeffree@g.ucla.edu>
 * @author James Wu <wuzhonglin@ucla.edu>
 */

#include "client.hpp"

using namespace std;

namespace sbt {

Client::Client(const std::string& port, const std::string& torrent) {
  nPort = port;
  nDownloaded = 0;
  nUploaded = 0;

  // Generate a randomized peer_id
  nPeerId = generatePeer();

  nInfo = new MetaInfo();
  nHttpResponse = new HttpResponse();
  nTrackerResponse = new TrackerResponse();

  // Read the torrent file into a filestream and decode
  ifstream torrentStream(torrent, ifstream::in);
  nInfo->wireDecode(torrentStream);
  fck();

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
 * Checks if file exists or not. If it doesn't, allocates space for it.
 * If it does, checks existing file against pieces and rellaocate as necessary.
 */
int Client::fck() {
  struct stat buffer;
  int rc;
  if ((rc = stat((nInfo->getName()).c_str(), &buffer)) == 0) { // file exists
    if (nDownloaded > nInfo->getLength()) {
      fd = open((nInfo->getName()).c_str(), O_RDWR);
      if (ftruncate(fd, nDownloaded) != 0) {
        fprintf(stderr, "%s\n", "File truncation failed");
        return RC_FILE_ALLOCATE_FAILED;
      }
    }
  } else {
    fd = open((nInfo->getName()).c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
    if ((rc = posix_fallocate(fd, 0, nInfo->getLength())) != 0) {
      fprintf(stderr, "File allocate error: %d\n", rc);
      return RC_FILE_ALLOCATE_FAILED;
    }
  }

  return 0;
}

int Client::bindClient(string& clientPort, string ipaddr) {
  int clientSockfd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in clientAddr;
  clientAddr.sin_family = AF_INET;
  clientAddr.sin_port = htons(atoi(clientPort.c_str()));
  clientAddr.sin_addr.s_addr = inet_addr(ipaddr.c_str());
  memset(clientAddr.sin_zero, '\0', sizeof(clientAddr.sin_zero));

  if (bind(clientSockfd, (struct sockaddr*) &clientAddr, sizeof(clientAddr)) == -1) {
    return RC_CLIENT_CONNECTION_FAILED;
  }

  return 0;
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

  // Prepare the request with a started event
  prepareRequest(getRequest, kStarted);
  int num_times = 0;

  // Retrieve the tracker's IP address
  string tip;
  resolveHost(nTrackerUrl, tip);
  bindClient(nPort, CLIENT_IP);

  // Keep the client running until tracker ends client
  while (true) {
    // Create socket using TCP IP
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // Connect to server using tracker's port
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(atoi(nTrackerPort.c_str()));
    serverAddr.sin_addr.s_addr = inet_addr(tip.c_str());
    memset(serverAddr.sin_zero, '\0', sizeof(serverAddr.sin_zero));

    // Connect to the server
    if (connect(sockfd, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) == -1) {
      fprintf(stderr, "Failed to connect to tracker at port: %d\n", ntohs(serverAddr.sin_port));
      return RC_TRACKER_CONNECTION_FAILED;
    }

    // Send GET request to the tracker
    if (send(sockfd, getRequest.c_str(), getRequest.size(), 0) == -1) {
      fprintf(stderr, "Failed to send GET request to tracker at port: %d\n", ntohs(serverAddr.sin_port));
      return RC_SEND_GET_REQUEST_FAILED;
    }

    // Initialize a new buffer to store the response message
    char buf[1000] = {'\0'};
    if (recv(sockfd, buf, sizeof(buf), 0) == -1) {
      fprintf(stderr, "Failed to receive a response from tracker.\n");
      return RC_NO_TRACKER_RESPONSE;
    }

    // Calculate the actual size of the response message
    int buf_size = 0;
    for(; buf[buf_size] != '\0'; buf_size++);

    // Check if the message was empty
    if (buf_size > 0) {
      // Parse the response into HttpResponse and put into a istream
      const char* res_body;
      res_body = nHttpResponse->parseResponse(buf, buf_size);
      istringstream responseStream(res_body);

      // Decode the dictionary obtained from the response
      bencoding::Dictionary dict;
      dict.wireDecode(responseStream);
      nTrackerResponse->decode(dict);

      // Check whether the tracker responded with a fail
      if (nTrackerResponse->isFailure()) {
        fprintf(stderr, "Fail:%s\n", nTrackerResponse->getFailure().c_str());
        return RC_TRACKER_RESPONSE_FAILED;
      }

      // Print the list of peers for the first response received
      /*if (num_times == 0) {
        std::vector<PeerInfo> peers = nTrackerResponse->getPeers();
        std::vector<PeerInfo>::iterator it = peers.begin();
        for (; it != peers.end(); it++) {
          cout << it->ip << ":" << it->port << endl;
        }
      }*/

      // Prepare a new request without any events
      prepareRequest(getRequest);
    }

    // Sleep for the interval we received from this either the previous or current response
    sleep(nTrackerResponse->getInterval());

    num_times++;

    // Close the sockfd so that we can create a new connection for non-persistent Http requests
    close(sockfd);
  }

  return 0;
}

/*
 * Resolves the tracker's hostname to its IP address.
 * Returns RC_GET_ADDRESS_INFO_FAILED upon failure. This requires a socket to
 * have been set up prior to calling.
 */
int Client::resolveHost(string& url, string& ip) {
  struct addrinfo hints, *res, *p;
  struct sockaddr_in *h;
  int rv;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET; // uses IPv4, can force IPv6 if necessary
  hints.ai_socktype = SOCK_STREAM;

  if ((rv = getaddrinfo(url.c_str(), nTrackerPort.c_str(), &hints, &res)) != 0) {
    fprintf(stderr, "Error getting address info: %s\n", gai_strerror(rv));
    return RC_GET_ADDRESS_INFO_FAILED;
  }

  // Connect to first possible result
  for (p = res; p != NULL; p = p->ai_next) {
    h = (struct sockaddr_in *)p->ai_addr;
    ip = inet_ntoa(h->sin_addr);
  }

  freeaddrinfo(res);
  return 0;
}

/*
 * Formats and prepares a GET request to the tracker's announce url.
 * Takes in an event type and returns the prepared request.
 */
int Client::prepareRequest(string& request, int event /*= kIgnore*/) {
  string url_f = "/%s?info_hash=%s&peer_id=%s&port=%s&uploaded=%d&downloaded=%d&left=%d";
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
    nUploaded,
    nDownloaded,
    url_left
  );
  string path = request_url;

  // Set up a HttpRequest and fill its parameters
  HttpRequest req;
  req.setHost(nTrackerUrl);
  req.setPort(atoi(nTrackerPort.c_str()));
  req.setMethod(HttpRequest::GET);
  req.setVersion("1.0");
  req.setPath(path);
  req.addHeader("Accept-Language", "en-US");

  size_t req_length = req.getTotalLength();
  char *buf = new char[req_length];
  req.formatRequest(buf);

  request = buf;
  return 0;
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
 * If the SIMPLEBT_TEST flag is enabled, always returns the test peer id.
 *
 * More info: https://wiki.theory.org/BitTorrentSpecification#peer_id
 */
string Client::generatePeer() {
  string peer_id = PEER_ID_PREFIX;
  for (int i = 0; i < 12; i++) {
    peer_id += to_string(rand() % 10);
  }

  return SIMPLEBT_TEST ? TEST_PEER_ID : peer_id;
}

} // namespace sbt
