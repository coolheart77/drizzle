/*
 * Copyright (c) 2010, Joseph Daly <skinny.moey@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright notice,
 *     this list of conditions and the following disclaimer in the documentation
 *     and/or other materials provided with the distribution.
 *   * Neither the name of Joseph Daly nor the names of its contributors
 *     may be used to endorse or promote products derived from this software
 *     without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef PLUGIN_LOGGING_STATS_SCORE_BOARD_SLOT_H
#define PLUGIN_LOGGING_STATS_SCORE_BOARD_SLOT_H

#include "user_commands.h"

#include <string>

class ScoreBoardSlot
{
public:
  ScoreBoardSlot(); 

  ~ScoreBoardSlot(); 

  UserCommands* getUserCommands();

  void setSessionId(uint64_t in_session_id);

  uint64_t getSessionId();

  void setInUse(bool in_in_use);

  bool isInUse();

  void setUser(std::string in_user);

  const std::string& getUser();

  void setIp(std::string in_ip);

  const std::string& getIp();

  void reset();

  void merge(ScoreBoardSlot *score_board_slot);

private:
  UserCommands *user_commands;
  std::string user;
  std::string ip;
  bool in_use;
  uint64_t session_id;
};
 
#endif /* PLUGIN_LOGGING_STATS_SCORE_BOARD_SLOT_H */
