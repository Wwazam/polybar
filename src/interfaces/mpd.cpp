#include <cstring>
#include <string>
#include <assert.h>
#include <thread>

#include "lemonbuddy.hpp"
#include "services/logger.hpp"
#include "interfaces/mpd.hpp"
#include "utils/math.hpp"

namespace mpd
{
  std::shared_ptr<Connection> conn;
  std::shared_ptr<Connection> &Connection::get()
  {
    if (!conn)
      conn = std::make_shared<Connection>();
    return conn;
  }


  // Base

  void Connection::connect()
  {
    assert(!this->connection);

    try {
      this->connection.reset(mpd_connection_new(this->host.c_str(), this->port, this->timeout * 1000));
      this->check_errors();

      if (!this->password.empty()) {
        this->noidle();
        assert(!this->mpd_command_list_active);
        mpd_run_password(this->connection.get(), this->password.c_str());
        this->check_errors();
      }

      this->mpd_fd = mpd_connection_get_fd(this->connection.get());
      this->check_errors();
    } catch(ClientError &e) {
      this->disconnect();
      throw &e;
    }
  }

  void Connection::disconnect()
  {
    this->connection.reset();
    this->mpd_idle = false;
    this->mpd_command_list_active = false;
  }

  bool Connection::connected()
  {
    return this->connection.get() != nullptr;
  }

  // bool Connection::retry_connection(int interval)
  // {
  //   if (this->connected())
  //     return true;
  //
  //   while (true) {
  //     try {
  //       this->connect();
  //       return true;
  //     } catch (Exception &e) {
  //       get_logger()->debug(e.what());
  //     }
  //
  //     std::this_thread::sleep_for(
  //       std::chrono::duration<double>(interval));
  //   }
  // }

  void Connection::idle()
  {
    this->check_connection();
    if (!this->mpd_idle) {
      mpd_send_idle(this->connection.get());
      this->check_errors();
    }
    this->mpd_idle = true;
  }

  int Connection::noidle()
  {
    this->check_connection();
    int flags = 0;
    if (this->mpd_idle && mpd_send_noidle(this->connection.get())) {
      this->mpd_idle = false;
      flags = mpd_recv_idle(this->connection.get(), true);
      mpd_response_finish(this->connection.get());
      this->check_errors();
    }
    return flags;
  }

  void Connection::check_connection()
  {
    if (!this->connection)
      throw ClientError("Not connected to MPD server", MPD_ERROR_STATE, false);
  }

  void Connection::check_prerequisites()
  {
    this->check_connection();
    this->noidle();
  }

  void Connection::check_prerequisites_commands_list()
  {
    this->noidle();
    assert(!this->mpd_command_list_active);
    this->check_prerequisites();
  }

  void Connection::check_errors()
  {
    auto connection = this->connection.get();
    mpd_error code = mpd_connection_get_error(connection);

    if (code == MPD_ERROR_SUCCESS)
      return;

    std::string msg = mpd_connection_get_error_message(connection);

    if (code == MPD_ERROR_SERVER)
      throw ServerError(msg,
          mpd_connection_get_server_error(connection),
          mpd_connection_clear_error(connection));
    else
      throw ClientError(msg, code, mpd_connection_clear_error(connection));
  }


  // Commands

  void Connection::play()
  {
    try {
      this->check_prerequisites_commands_list();
      mpd_run_play(this->connection.get());
      this->check_errors();
    } catch (Exception &e) {
      log_error(e.what());
    }
  }

  void Connection::pause(bool state)
  {
    try {
      this->check_prerequisites_commands_list();
      mpd_run_pause(this->connection.get(), state);
      this->check_errors();
    } catch (Exception &e) {
      log_error(e.what());
    }
  }

  // void Connection::toggle()
  // {
  //   try {
  //     this->check_prerequisites_commands_list();
  //     mpd_run_toggle_pause(this->connection.get());
  //     this->check_errors();
  //   } catch (Exception &e) {
  //     log_error(e.what());
  //   }
  // }

  void Connection::stop()
  {
    try {
      this->check_prerequisites_commands_list();
      mpd_run_stop(this->connection.get());
      this->check_errors();
    } catch (Exception &e) {
      log_error(e.what());
    }
  }

  void Connection::prev()
  {
    try {
      this->check_prerequisites_commands_list();
      mpd_run_previous(this->connection.get());
      this->check_errors();
    } catch (Exception &e) {
      log_error(e.what());
    }
  }

  void Connection::next()
  {
    try {
      this->check_prerequisites_commands_list();
      mpd_run_next(this->connection.get());
      this->check_errors();
    } catch (Exception &e) {
      log_error(e.what());
    }
  }

  void Connection::seek(int percentage)
  {
    try {
      auto status = this->get_status();
      if (status->total_time == 0)
        return;
      if (percentage < 0)
        percentage = 0;
      else if (percentage > 100)
        percentage = 100;
      int pos = float(status->total_time) * percentage / 100.0f + 0.5f;
      this->check_prerequisites_commands_list();
      mpd_run_seek_id(this->connection.get(), status->song_id, pos);
      this->check_errors();
    } catch (Exception &e) {
      log_error(e.what());
    }
  }

  void Connection::set_repeat(bool mode)
  {
    try {
      this->check_prerequisites_commands_list();
      mpd_run_repeat(this->connection.get(), mode);
      this->check_errors();
    } catch (Exception &e) {
      log_error(e.what());
    }
  }

  void Connection::set_random(bool mode)
  {
    try {
      this->check_prerequisites_commands_list();
      mpd_run_random(this->connection.get(), mode);
      this->check_errors();
    } catch (Exception &e) {
      log_error(e.what());
    }
  }

  void Connection::set_single(bool mode)
  {
    try {
      this->check_prerequisites_commands_list();
      mpd_run_single(this->connection.get(), mode);
      this->check_errors();
    } catch (Exception &e) {
      log_error(e.what());
    }
  }


  // Status

  std::unique_ptr<Status> Connection::get_status()
  {
    this->check_prerequisites();
    mpd_status *status = mpd_run_status(this->connection.get());
    this->check_errors();
    return std::make_unique<Status>(status);
  }

  Status::Status(struct mpd_status *status) {
    this->set(std::unique_ptr<struct mpd_status, StatusDeleter> {status});
  }

  void Status::set(std::unique_ptr<struct mpd_status, StatusDeleter> status)
  {
    this->status.swap(status);

    this->song_id = mpd_status_get_song_id(this->status.get());
    this->random = mpd_status_get_random(this->status.get());
    this->repeat = mpd_status_get_repeat(this->status.get());
    this->single = mpd_status_get_single(this->status.get());
    this->elapsed_time = mpd_status_get_elapsed_time(this->status.get());
    this->total_time = mpd_status_get_total_time(this->status.get());

    this->updated_at = std::chrono::system_clock::now();
  }

  void Status::update(int event)
  {
    auto status = Connection::get()->get_status();

    if (event & (MPD_IDLE_PLAYER | MPD_IDLE_OPTIONS)) {
      this->set(std::move(status->status));
      this->elapsed_time_ms = this->elapsed_time * 1000;

      this->song = Connection::get()->get_song();

      auto mpd_state = mpd_status_get_state(this->status.get());

      switch (mpd_state) {
        case MPD_STATE_PAUSE: this->state = PAUSED; break;
        case MPD_STATE_PLAY: this->state = PLAYING; break;
        case MPD_STATE_STOP: this->state = STOPPED; break;
        default: this->state = UNKNOWN;
      }
    }
  }

  void Status::update_timer()
  {
    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now() - this->updated_at);

    this->elapsed_time_ms += dur.count();
    this->elapsed_time = this->elapsed_time_ms / 1000 + 0.5f;
    this->updated_at = std::chrono::system_clock::now();
  }

  // unsigned Status::get_total_time() {
  //   return this->total_time;
  // }

  // unsigned Status::get_elapsed_time() {
  //   return this->elapsed_time;
  // }

  unsigned Status::get_elapsed_percentage()
  {
    if (this->total_time == 0) return 0;
    return (int) float(this->elapsed_time) / float(this->total_time) * 100 + 0.5f;
  }

  std::string Status::get_formatted_elapsed()
  {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lu:%02lu", this->elapsed_time / 60, this->elapsed_time % 60);
    return std::string(buffer);
  }

  std::string Status::get_formatted_total()
  {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%lu:%02lu", this->total_time / 60, this->total_time % 60);
    return std::string(buffer);
  }


  // Song

  std::unique_ptr<Song> Connection::get_song()
  {
    this->check_prerequisites_commands_list();
    mpd_send_current_song(this->connection.get());
    mpd_song *song = mpd_recv_song(this->connection.get());
    mpd_response_finish(this->connection.get());
    this->check_errors();

    if (song == nullptr)
      return std::make_unique<Song>();
    else
      return std::make_unique<Song>(song);
  }

  Song::Song(struct mpd_song *song)
    : song(std::shared_ptr<struct mpd_song>(song, mpd_song_free))
  {
  }

  std::string Song::get_artist()
  {
    assert(this->song);
    auto tag = mpd_song_get_tag(this->song.get(), MPD_TAG_ARTIST, 0);
    if (tag == nullptr)
      return "";
    return std::string(tag);
  }

  std::string Song::get_album()
  {
    assert(this->song);
    auto tag = mpd_song_get_tag(this->song.get(), MPD_TAG_ALBUM, 0);
    if (tag == nullptr)
      return "";
    return std::string(tag);
  }

  std::string Song::get_title()
  {
    assert(this->song);
    auto tag = mpd_song_get_tag(this->song.get(), MPD_TAG_TITLE, 0);
    if (tag == nullptr)
      return "";
    return std::string(tag);
  }

  // unsigned Song::get_duration()
  // {
  //   assert(this->song);
  //   return mpd_song_get_duration(this->song.get());
  // }
}
