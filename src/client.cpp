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

  // Initialize bitfield
  initBitfield();

  nRemaining = nInfo->getLength();
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

  close(clientSockfd);
}

/*
 * Checks if file exists or not. If it doesn't, allocates space for it.
 * If it does, checks existing file against pieces and rellaocate as necessary.
 */
int Client::fck() {
  struct stat buffer;
  FILE *fd;

  vector<uint8_t>::iterator begin, end;
  vector<uint8_t> pieces = nInfo->getPieces();

  fd = fopen((nInfo->getName()).c_str(), "a+");
  if (fd) {
    stat((nInfo->getName()).c_str(), &buffer);
    int size = nInfo->getLength() > nDownloaded ? nInfo->getLength() : nDownloaded;

    if (buffer.st_size < size) {
      if (ftruncate(fileno(fd), size) != 0) {
        fprintf(stderr, "File truncation failed: %d\n", errno);
        return RC_FILE_ALLOCATE_FAILED;
      }
    }

    // go through each piece in the file and compare the hash
    begin = pieces.begin();
    end = begin + PIECE_HASH;

    int piece_hash_count = pieces.size() / PIECE_HASH;
    int pieces_left = piece_hash_count;

    char *piece = new char[nInfo->getPieceLength()];
    while (!feof(fd)) {
      vector<uint8_t> c_piece(begin, end);
      size_t length = fread(piece, sizeof(char) * nInfo->getPieceLength(), 1, fd);
      ConstBufferPtr piece_hash = util::sha1(make_shared<sbt::Buffer>(piece, length));

      if (*piece_hash == c_piece) {
        fprintf(stderr, "Piece %d validated\n", piece_hash_count - pieces_left);

        // set bitfield
      }

      pieces_left--;
      begin += PIECE_HASH;
      end += PIECE_HASH;
    }
  } else {
    fprintf(stderr, "File allocate error: %d\n", errno);
    return RC_FILE_ALLOCATE_FAILED;
  }

  return 0;
}

/*
 * Initializes the client's bitfield to all zeroes. Requires that the torrent
 * metainfo have been parsed first.
 */
void Client::initBitfield() {
  int file_length = nInfo->getLength();
  int pieces_length = nInfo->getPieceLength();
  int piece_count = (file_length + pieces_length - 1) / pieces_length;

  nFieldSize = (piece_count + 7) / 8;
  //nBitfield = new Buffer(nFieldSize);
  cout << "pieces are is " << piece_count << endl;
  cout << "size is " << nFieldSize << endl;
  //nBitfield = (char *) malloc(nFieldSize);
  nBitfield = new uint8_t[nFieldSize];
  //memset(nBitfield, 0, nFieldSize);
}

/*
 *  Binds the client to the specified IP address and port.
 */
int Client::bindClient(string& clientPort, string ipaddr) {
  // Create a new socket for the client
  clientSockfd = socket(AF_INET, SOCK_STREAM, 0);
  int yes = 1;
  if (setsockopt(clientSockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
    fprintf(stderr, "Could not set socket options\n");
    return RC_CLIENT_CONNECTION_FAILED;
  }

  // Initialize the client address information
  struct sockaddr_in clientAddr;
  clientAddr.sin_family = AF_INET;
  clientAddr.sin_port = htons(atoi(clientPort.c_str()));
  clientAddr.sin_addr.s_addr = inet_addr(ipaddr.c_str());
  memset(clientAddr.sin_zero, '\0', sizeof(clientAddr.sin_zero));

  // Bind the client to the port
  if (bind(clientSockfd, (struct sockaddr*) &clientAddr, sizeof(clientAddr)) == -1) {
    return RC_CLIENT_CONNECTION_FAILED;
  }

  // Listen on this socket
  if (listen(clientSockfd, 1) == -1) {
    fprintf(stderr, "Cannot listen on port: %s\n", nPort.c_str());
    return RC_CLIENT_CONNECTION_FAILED;
  }

  // Accept a connection
  struct sockaddr_in peerAddr;
  socklen_t peerAddrLen;
  int peerSockfd = accept(clientSockfd, (struct sockaddr*) &peerAddr, &peerAddrLen);
cout << "accepted" << endl;
  if (peerSockfd == -1) {
    fprintf(stderr, "Could not accept connection from peer\n");
    // return RC_PEER_ACCEPT_FAILED;
    return 0;
  } else {
    char buf[BUFFER_SIZE] = {'\0'};
    if (recv(peerSockfd, buf, sizeof(buf), 0) == -1) {
      fprintf(stderr, "Failed to receive a response from tracker.\n");
      return RC_NO_TRACKER_RESPONSE;
    }
    fprintf(stdout, "Received a connection from the peer with ip:port %d:%d\n", peerAddr.sin_addr.s_addr, peerAddr.sin_port);
  }

  /*HandShake* tempHandshake = new HandShake();

  sockArray.push_back(peerSockfd);
  PeerInfo tempPeer = new PeerInfo();
  tempPeer.ip = peerAddr.
  tempPeer.port = peerAddr.sin_port;*/

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

    //fprintf(stderr, "Started connecting to the tracker\n");
    // Create socket and connect to port using TCP IP
    createConnection(tip, nTrackerPort, sockfd);

    // Send GET request to the tracker
    if (send(sockfd, getRequest.c_str(), getRequest.size(), 0) == -1) {
      fprintf(stderr, "Failed to send GET request to tracker at port: %s\n", nTrackerPort.c_str());
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

      peers = nTrackerResponse->getPeers();
      vector<PeerInfo>::iterator it = peers.begin();
      for (; it != peers.end(); it++) {
        string t_pip = it->ip;
        int t_pport = it->port;
        pAttr t_pAttr(t_pip, t_pport);
        cout << it->ip << ":" << it->port << endl;
        if (it->port != atoi(nPort.c_str()) && find(hasPeerConnected.begin(), hasPeerConnected.end(), t_pAttr) == hasPeerConnected.end()) {
          int peerSockfd = socket(AF_INET, SOCK_STREAM, 0);
          fprintf(stderr, "Setting up handshake with a peer\n");
          prepareHandshake(peerSockfd, nInfo->getHash(), *it);

          sockArray.push_back(peerSockfd);
          socketToPeer[peerSockfd] = *it;

          hasPeerConnected.push_back(t_pAttr);
        }
      }

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

int Client::createConnection(string ip, string port, int &sockfd) {
    // Create socket using TCP IP
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // Connect to server using tracker's port
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(atoi(port.c_str()));
    serverAddr.sin_addr.s_addr = inet_addr(ip.c_str());
    memset(serverAddr.sin_zero, '\0', sizeof(serverAddr.sin_zero));

    // Connect to the server
    if (connect(sockfd, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) != 0) {
      fprintf(stderr, "Failed to connect to tracker port: %d\n", serverAddr.sin_port);
      return RC_TRACKER_CONNECTION_FAILED;
    }

    return 0;
}

int Client::createConnection(string ip, uint16_t port, int &sockfd) {
    // Create socket using TCP IP
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    // Connect to server using tracker's port
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(ip.c_str());
    memset(serverAddr.sin_zero, '\0', sizeof(serverAddr.sin_zero));

    fprintf(stderr, "Peer I am connecting to has port of %d\n", port);

    // Connect to the server
    if (connect(sockfd, (struct sockaddr*) &serverAddr, sizeof(serverAddr)) == -1) {
      fprintf(stderr, "Failed to connect to peer port: %d\n", serverAddr.sin_port);
      return RC_TRACKER_CONNECTION_FAILED;
    }

    return 0;
}

int Client::sendPayload(int& sockfd, msg::MsgBase& payload, pAttr peer) {
  ConstBufferPtr enc_msg = payload.encode();
  const char* b_msg = reinterpret_cast<const char*>(enc_msg->buf());
  cout << "size is " << enc_msg->size() << endl;
  cout << "size could be " << strlen(b_msg) << endl;
  if (send(sockfd, b_msg, 8, 0) < 0) {
    fprintf(stderr, "Failed to send payload to peer %s:%d\n", peer.first.c_str(), peer.second);
    return RC_SEND_GET_REQUEST_FAILED;
  }

  return 0;
}

int Client::prepareHandshake(int &sockfd, ConstBufferPtr infoHash, PeerInfo peer) {
  nHandshake = new msg::HandShake(infoHash, nPeerId);
  ConstBufferPtr encodedShake = nHandshake->encode();

  fprintf(stderr, "Initiating handshake with the peers\n");
  createConnection(peer.ip, peer.port, sockfd);
  //createConnection(peer.ip, "11111", sockfd);

  const char* shakeMsg = reinterpret_cast<const char*>(encodedShake->buf());

  // Send handshake to peer
  if (send(sockfd, shakeMsg, 68, 0) == -1) {
    fprintf(stderr, "Failed to send handshake to port: %d\n", peer.port);
    return RC_SEND_GET_REQUEST_FAILED;
  }

  // Initialize a new buffer to store the Handshake response
  char hs_buf[100000] = {'\0'};
  ssize_t n_buf_size = 0;
  if ((n_buf_size = recv(sockfd, hs_buf, sizeof(hs_buf), 0)) == -1) {
    fprintf(stderr, "Failed to receive a response from peer.\n");
    return RC_NO_TRACKER_RESPONSE;
  }
  fprintf(stderr, "buffer has length of %d\n", (int)n_buf_size);
  // Calculate the actual size of the response message
  ConstBufferPtr hs_res = make_shared<sbt::Buffer>(hs_buf, n_buf_size);

  string t_pip = socketToPeer[sockfd].ip;
  int t_pport = socketToPeer[sockfd].port;
  pAttr t_pAttr(t_pip, t_pport);
  parseMessage(sockfd, hs_res, t_pAttr);


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
    nTrackerEndpoint.c_str(),
    url_hash,
    url_id,
    nPort.c_str(),
    nUploaded,
    nDownloaded,
    nRemaining
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
 * Generic function for handling all incoming messages received
 * by the client. Differentiates between handshakes and any
 * other kind of message, and takes appropriate actions to respond.
 */
int Client::parseMessage(int& sockfd, ConstBufferPtr msg, pAttr peer) {
  try {
    msg::HandShake *handshake = new msg::HandShake();
    handshake->decode(msg);
    sendBitfield(sockfd, peer);
    fprintf(stderr, "The peer's peer id is %s\n", (handshake->getPeerId()).c_str());
  } catch (msg::Error e) { // was not a handshake
    switch (lastRektMsgType[peer]) {
      case msg::MSG_ID_INTERESTED: // expect unchoke
        break;
      case msg::MSG_ID_HAVE: // expect request
      case msg::MSG_ID_UNCHOKE:
        break;
      case msg::MSG_ID_BITFIELD: // expect bitfield if not already received
        handleBitfield(msg, peer);
        break;
      case msg::MSG_ID_REQUEST: // expect piece
        break;
      case msg::MSG_ID_PIECE: // expect request
        break;
      default:
        break;
    }

    // always expect have or interested
  }

  return 0;
}

int Client::sendBitfield(int &sockfd, pAttr peer) {
  ConstBufferPtr msg = make_shared<sbt::Buffer>(nBitfield, sizeof(nBitfield) - 1);
  msg::Bitfield bitfield_msg = msg::Bitfield(msg);
  sendPayload(sockfd, bitfield_msg, peer);

  receiveBitfield(sockfd, peer);

  return 0;
}

int Client::handleBitfield(ConstBufferPtr msg, pAttr peer) {
  fprintf(stderr, "We are now handling the bitfield\n");
  return 0;
}

int Client::receiveBitfield(int& sockfd, pAttr peer) {
  //int sockfd = peerToSocket.find(peer)->second;
  char hs_buf[1000] = {'\0'};
  ssize_t n_buf_size = 0;
  if ((n_buf_size = recv(sockfd, hs_buf, sizeof(hs_buf), 0)) == -1) {
    fprintf(stderr, "Failed to receive a bitfield from peer.\n");
    return RC_NO_TRACKER_RESPONSE;
  }
  ConstBufferPtr hs_res = make_shared<sbt::Buffer>(hs_buf, n_buf_size);
  fprintf(stderr, "bitfield has length of %d\n", (int)n_buf_size);
  fprintf(stderr, "peer's bitfield is %s\n", hs_buf);

  lastRektMsgType[peer] = msg::MSG_ID_BITFIELD;

  parseMessage(sockfd, hs_res, peer);
  return 0;
}

// Proof of concept
int Client::sendUnchoke(pAttr peer) {
  msg::Unchoke *unchoke = new msg::Unchoke();
  //sendPayload(*unchoke, peer);
  return 0;
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
