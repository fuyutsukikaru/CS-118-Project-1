/**
 * Copyright (C) 2015 by Codifica
 * Redistribution of this file is permitted under the terms of the GNU
 * Public License (GPL)
 *
 * @date 1/24/2015
 * @author James Wu <wuzhonglin@ucla.edu>
 */

#ifndef CODES_HPP
#define CODES_HPP

#define CLIENT_IP   "127.0.0.1"
#define TRACKER_IP  "127.0.0.1"

const int RC_CLIENT_CONNECTION_FAILED     = -1001;
const int RC_TRACKER_CONNECTION_FAILED    = -1002;
const int RC_SEND_GET_REQUEST_FAILED      = -1003;
const int RC_NO_TRACKER_RESPONSE          = -1004;
const int RC_TRACKER_RESPONSE_FAILED      = -1005;
const int RC_INVALID_URL                  = -1006;
const int RC_GET_ADDRESS_INFO_FAILED      = -1007;

#endif // CODES_HPP
