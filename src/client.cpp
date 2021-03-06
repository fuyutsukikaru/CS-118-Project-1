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
    nPieceCount = piece_hash_count;

    char *piece = new char[nInfo->getPieceLength()];
    while (!feof(fd)) {
      int index = piece_hash_count - pieces_left;
      vector<uint8_t> c_piece(begin, end);
      size_t length = fread(piece, sizeof(char) * nInfo->getPieceLength(), 1, fd);
      ConstBufferPtr piece_hash = util::sha1(make_shared<sbt::Buffer>(piece, length));

      if (*piece_hash == c_piece) {
        fprintf(stderr, "Piece %d validated\n", index);
        int byte = index / 8;
        int offset = index % 8;
        uint8_t mask = 1;
        if (((nBitfield[byte] >> offset) & mask) != 1) {
          nBitfield[byte] |= mask << offset;
        }
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

int Client::fpck(int index, int length) {
  vector<uint8_t> pieces = nInfo->getPieces();
  cout << "OUR PIECE HAS LENGTH " << length << endl;
  ifstream fp;
  fp.open(nInfo->getName(), ios::in | ios::binary);
  fp.seekg(index * nInfo->getPieceLength());
  uint8_t* piece = new uint8_t[length];
  fp.read((char *)piece, length);
  ConstBufferPtr piece_hash = util::sha1(make_shared<Buffer>(piece, length));

  bool valid = true;
  for (int i = 0; i < PIECE_HASH; i++) {
    if (pieces[(index * PIECE_HASH) + i] != piece_hash->get()[i]) {
      valid = false;
    }
  }

  if (!valid) {
    fprintf(stderr, "Piece hash check failed\n");
      cout << "PIECE NOT VALID" << endl;
      cout << "PIECE: ";
      for (int j = 0; j < length; j++) {
        cout << piece[j];
      }
      cout << endl;
      cout << "OUR HASH: ";
      for (int j = 0; j < PIECE_HASH; j++) {
        cout << int(piece_hash->get()[j]);
      }
      cout << endl;
      cout << "INFO HASH: ";
      for (int j = 0; j < PIECE_HASH; j++) {
        cout << int(pieces[(index * PIECE_HASH) + j]);
      }
      cout << endl;

    return RC_PIECE_NOT_VALID;
  }

  fprintf(stderr, "Piece %d validated\n", index);
  int byte = index / 8;
  int offset = index % 8;
  uint8_t mask = 1;
  nBitfield[byte] |= mask << (7 - offset);

  delete [] piece;
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
  nBitfield = new uint8_t[nFieldSize];
  memset(nBitfield, 0, nFieldSize);
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

  /*fprintf(stderr, "We are now listening on the port %s\n", nPort.c_str());

  // Accept a connection
  struct sockaddr_in peerAddr;
  socklen_t peerAddrLen;
  fprintf(stderr, "Awaiting to accept a connection from a peer\n");
  int peerSockfd = accept(clientSockfd, (struct sockaddr*) &peerAddr, &peerAddrLen);
  if (peerSockfd == -1) {
    fprintf(stderr, "Could not accept connection from peer\n");
    // return RC_PEER_ACCEPT_FAILED;
    return 0;
  }

  fprintf(stderr, "We've accepted a new connection\n");

  char buf[BUFFER_SIZE] = {'\0'};
  ssize_t buf_size = 0;
  if ((buf_size = recv(peerSockfd, buf, sizeof(buf), 0)) == -1) {
    fprintf(stderr, "Failed to receive a response from tracker.\n");
    return RC_NO_TRACKER_RESPONSE;
  }
  fprintf(stderr, "Received a connection from the peer with ip:port %d:%d\n", peerAddr.sin_addr.s_addr, peerAddr.sin_port);

  string pip;
  ostringstream convert;
  convert << peerAddr.sin_addr.s_addr;
  pip = convert.str();
  fprintf(stderr, "Peer's ip is %s\n", pip.c_str());
  int pport = peerAddr.sin_port;
  pAttr t_pAttr(pip, pport);

  Peer peerstat;

  peerStatus[t_pAttr] = peerstat;
  sockArray.push_back(peerSockfd);
  hasPeerConnected.push_back(t_pAttr);*/
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
    char buf[BUFFER_SIZE] = {'\0'};
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

    }

    nitroConnect(2);
    nitroConnect(2);

    // Prepare a new request without any events
    if (nDownloaded < nInfo->getLength()) {
      prepareRequest(getRequest);
    } else {
      cout << "COMPLETED, NOW TELLING TRACKER" << endl;
      prepareRequest(getRequest, kCompleted);
    }

    num_times++;

    // Close the sockfd so that we can create a new connection for non-persistent Http requests
    close(sockfd);
  }

  return 0;
}

/*
 * This connect function has flames painted on it so it goes faster
 */
int Client::nitroConnect(int sleep_count) {
    // Loop through the list of peers you're connected to
    vector<int>::iterator iter = sockArray.begin();
    for (; iter != sockArray.end(); iter++) {
      string t_pip = socketToPeer[*iter].ip;
      int t_pport = socketToPeer[*iter].port;
      pAttr t_pAttr(t_pip, t_pport);

      // Send an interested to every peer you're connected to
      if (!peerStatus[t_pAttr].unchoked) {
        sendInterested(*iter, t_pAttr);
      }

      // If the peer is unchoked, then send a request for a piece you don't have
      if (peerStatus[t_pAttr].unchoked) {
        sendRequest(*iter, t_pAttr);
      }
    }

    sleep(nTrackerResponse->getInterval() / sleep_count);

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

  int msg_length = 5;
  if (payload.getPayload() != NULL) {
    // Calculate the size of the payload
    msg_length += payload.getPayload()->size();
  }

  // Recast the buffer into a format to send to peer
  const char* b_msg = reinterpret_cast<const char*>(enc_msg->buf());
  if (send(sockfd, b_msg, msg_length, 0) < 0) {
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

  const char* shakeMsg = reinterpret_cast<const char*>(encodedShake->buf());

  // Send handshake to peer
  if (send(sockfd, shakeMsg, 68, 0) == -1) {
    fprintf(stderr, "Failed to send handshake to port: %d\n", peer.port);
    return RC_SEND_GET_REQUEST_FAILED;
  }

  // Initialize a new buffer to store the Handshake response
  char hs_buf[BUFFER_SIZE] = {'\0'};
  ssize_t n_buf_size = 0;
  if ((n_buf_size = recv(sockfd, hs_buf, sizeof(hs_buf), 0)) == -1) {
    fprintf(stderr, "Failed to receive a response from peer.\n");
    return RC_NO_TRACKER_RESPONSE;
  }
  fprintf(stderr, "buffer has length of %d\n", (int)n_buf_size);
  // Calculate the actual size of the response message
  ConstBufferPtr hs_res = make_shared<sbt::Buffer>(hs_buf, n_buf_size);

  // this works
  pAttr t_pAttr(peer.ip, peer.port);

  Peer peerInfo;
  peerInfo.sentHandshake = true;

  peerStatus[t_pAttr] = peerInfo;
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


  const char* url_hash = url::encode((const uint8_t *)(nInfo->getHash()->get()), 20).c_str();
  const char* url_id = url::encode((const uint8_t *)nPeerId.c_str(), 20).c_str();

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
    const uint8_t* header = msg->get();

    // we should be periodically sending haves to all our peers
    // at the same time, we periodically send interesteds to choked peers
    // and requests to unchoked peers. this should be independent of this
    // function.

    // check the message id (the fifth bit)
    switch(header[4]) {
      case msg::MSG_ID_INTERESTED:
        // send an unchoke
        // unless we are being mean. then do not.
        break;
      case msg::MSG_ID_HAVE:
        // update the local instance of the peer's bitfield
        // send a request? no we should be sending requests anyway...
        break;
      case msg::MSG_ID_UNCHOKE:
        // mark this peer as unchoked and send an interested
        handleUnchoke(msg, peer);
        break;
      case msg::MSG_ID_BITFIELD:
        // update our local instance of the peer's bitfield
        // then, if we're already unchoked, send a request
        // if not, send an interested
        handleBitfield(msg, peer);
        break;
      case msg::MSG_ID_REQUEST:
        // send the requested piece
        // increase our uploaded
        break;
      case msg::MSG_ID_PIECE:
        // write the piece to our local file
        // increase our downloaded
        handlePiece(msg, peer);
        break;
      default:
        break;
    }
  }

  return 0;
}

int Client::sendBitfield(int& sockfd, pAttr peer) {
  ConstBufferPtr msg = make_shared<sbt::Buffer>(nBitfield, nFieldSize);
  msg::Bitfield bitfield_msg = msg::Bitfield(msg);
  sendPayload(sockfd, bitfield_msg, peer);

  receivePayload(sockfd, peer);

  return 0;
}

int Client::sendRequest(int& sockfd, pAttr peer) {
  // create a bitmask to deal with the bitfield
  uint8_t mask = 1;

  const uint8_t* peers_bitfield = peerBitfields[peer];
/*
  cout << "CANDIDATE BITFIELD IS ";
  for (int i = 0; i < nFieldSize; i++) {
    for (int j = 7; j >= 0; j--) {
      cout << ((peers_bitfield[i] >> j) & mask);
    }
  }
  cout << endl;
  cout << "OUR BITFIELD IS ";
  for (int i = 0; i < nFieldSize; i++) {
    for (int j = 7; j >= 0; j--) {
      cout << ((nBitfield[i] >> j) & mask);
    }
  }
  cout << endl;*/


  if (peers_bitfield != NULL) {
    for (int i = 0; i < nFieldSize; i++) {
      for (int j = 7; j >= 0; j--) {
        uint8_t candidate_bit = (peers_bitfield[i] >> j) & mask;
        uint8_t bitfield_bit = (nBitfield[i] >> j) & mask;

        // only request if we're missing the piece and they have the piece
        if (candidate_bit == 1 && bitfield_bit != 1) {
          unsigned int index = (i * 8) + (7 - j);
          cout << "Requesting piece " << index << endl;

          int len = nInfo->getPieceLength();
          if (index == nPieceCount - 1) {
            len = nInfo->getLength() % nInfo->getPieceLength();
          }

          msg::Request request_msg = msg::Request(index, 0, len);
          sendPayload(sockfd, request_msg, peer);

          receivePayload(sockfd, peer);
          // deal with receiving the piece after
          return 0;
        }
      }
    }
  } else {
    // some error for empty bitfield
    cout << "peer does not have anything" << endl;
  }

  return 0;
}

int Client::sendInterested(int& sockfd, pAttr peer) {
  msg::Interested intr_msg = msg::Interested();
  sendPayload(sockfd, intr_msg, peer);

  receivePayload(sockfd, peer);
  return 0;
}

int Client::sendHave(int& sockfd, pAttr peer, unsigned int index) {
  msg::Have have = msg::Have(index);
  sendPayload(sockfd, have, peer);

  return 0;
}

// Proof of concept
int Client::sendUnchoke(pAttr peer) {
  //msg::Unchoke *unchoke = new msg::Unchoke();
  //sendPayload(*unchoke, peer);
  return 0;
}


int Client::handleBitfield(ConstBufferPtr msg, pAttr peer) {
  fprintf(stderr, "We are now handling the bitfield\n");
  msg::Bitfield* tempBitfield = new msg::Bitfield();

  // Decode the bitfield message to get the actual bitfield
  tempBitfield->decode(msg);
  const uint8_t* b_msg = (tempBitfield->getBitfield())->buf();

  // Store the peer's bitfield in a map
  peerBitfields[peer] = b_msg;

  return 0;
}

int Client::handlePiece(ConstBufferPtr msg, pAttr peer) {
  msg::Piece* piece = new msg::Piece();
  piece->decode(msg);

  const uint32_t index = piece->getIndex();
  ConstBufferPtr block = piece->getBlock();
/*
  for (int i = 0; i < block->size(); i++) {
    cout << block->get()[i];
  }
  cout << endl;*/

  int rc;

  ofstream fp;
  fp.open(nInfo->getName(), ios::in | ios::out | ios::binary);
  fp.seekp(index * nInfo->getPieceLength(), ios::beg);

  int len = nInfo->getPieceLength();
  if (index == nPieceCount - 1) {
    len = nInfo->getLength() % nInfo->getPieceLength();
  }

  fp.write((char *)(block->get()), len);

  if ((rc = fpck(index, len)) < 0) {
    fprintf(stderr, "Piece validation failed: %d\n", rc);
    return RC_PIECE_NOT_VALID;
  } else {
    nDownloaded += len;
    nRemaining -= len;
    fprintf(stderr, "We received %d\n", len);
  }

  // now we have the piece, so we send a have to everyone
  vector<int>::iterator iter = sockArray.begin();
  for (; iter != sockArray.end(); iter++) {
    sendHave(*iter, peer, index);
  }

  return 0;
}

int Client::handleUnchoke(ConstBufferPtr msg, pAttr peer) {
  fprintf(stderr, "We are now handling an unchoke message\n");

  // Set peer status to unchoked so that we can begin sending requests
  peerStatus[peer].unchoked = true;
  return 0;
}

int Client::receivePayload(int& sockfd, pAttr peer) {
  uint8_t hs_buf[BUFFER_SIZE] = {'\0'};
  ssize_t n_buf_size = 0;
  if ((n_buf_size = recv(sockfd, hs_buf, sizeof(hs_buf), 0)) == -1) {
    fprintf(stderr, "Failed to receive payload from peer.\n");
    return RC_NO_TRACKER_RESPONSE;
  }
  ConstBufferPtr hs_res = make_shared<sbt::Buffer>(hs_buf, n_buf_size);
  fprintf(stderr, "payload has length of %d\n", (int)n_buf_size);

  parseMessage(sockfd, hs_res, peer);
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
