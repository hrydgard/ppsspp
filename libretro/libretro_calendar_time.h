#pragma once

#include <libretro.h>

// Copied from libretro.h; drop this file once libretro-common has it.
#ifndef RETRO_ENVIRONMENT_GET_CALENDAR_TIME_INTERFACE

/**
 * Gets an interface for reading calendar time and the time zone.
 *
 * Unlike the monotonic timers of \c RETRO_ENVIRONMENT_GET_PERF_INTERFACE,
 * this clock follows the calendar. It need not match the host's: a frontend
 * may offset it or hold it fixed.
 *
 * A core that emulates a clock reads the start time once, advances it by
 * emulated time and keeps the result in its savestates. If the game sets the
 * clock, the core stores the difference from this clock in its own save data;
 * the frontend's clock cannot be set.
 *
 * May be called from \c retro_init onwards. The function pointers stay valid
 * until \c retro_deinit, may be called from any thread from the start of
 * \c retro_load_game until \c retro_unload_game, and never block.
 *
 * @param[in,out] data <tt>struct retro_calendar_time_interface *</tt>.
 * The core sets \c interface_version to the highest version it supports.
 * The frontend sets it to the version it provides, which is never higher,
 * and fills only the fields of that version.
 * @return \c true if the frontend provided the interface.
 * @see retro_calendar_time_interface
 */
#define RETRO_ENVIRONMENT_GET_CALENDAR_TIME_INTERFACE (107 | RETRO_ENVIRONMENT_EXPERIMENTAL)

#define RETRO_CALENDAR_TIME_INTERFACE_VERSION 1

/**
 * A point in calendar time.
 */
struct retro_calendar_time
{
   /** UTC seconds since 1970-01-01 00:00:00, without leap seconds. */
   int64_t sec;

   /**
    * Nanoseconds past \c sec, 0 to 999999999. Precision depends on the
    * host; a host that has whole seconds only sets 0.
    */
   int32_t nsec;

   /**
    * Seconds east of UTC for the emulated local clock, DST included.
    * Local time is \c sec + \c utc_offset read as UTC.
    * Does not change while content is loaded.
    */
   int32_t utc_offset;
};

/**
 * Gets the time at which the content was loaded.
 *
 * The value does not change while the content stays loaded. The core advances
 * it by emulated time, keeps it running across \c retro_reset as a battery
 * clock would, and keeps the result in its savestates, so netplay peers and
 * input replays, which start from a savestate, see the same clock. After
 * loading a savestate the core uses the clock stored in that state and does
 * not read this again.
 *
 * @param[out] time Filled on success.
 * @return \c false if no calendar time is available.
 */
typedef bool (RETRO_CALLCONV *retro_calendar_get_start_time_t)(
      struct retro_calendar_time *time);

/**
 * Gets the current time on the same clock as \c get_start_time.
 *
 * Fails while the frontend holds the clock fixed, such as during netplay or
 * while recording or playing an input replay.
 * The core then advances the start time by emulated time. If this fails at
 * load, the core does not add the time that passed between sessions.
 *
 * Read it at load, at reset, when writing save data, or between frames to
 * correct drift from pausing and fast-forward. Reading it on every access to
 * an emulated clock register breaks run-ahead.
 *
 * Run-ahead, preemptive frames and rewind re-run frames from a savestate
 * without holding this clock, so a re-run frame can read a later time than
 * the original; emulation must not depend on differences within a frame.
 *
 * @param[out] time Filled on success.
 * @return \c false if the clock is held fixed or no calendar time is
 * available.
 */
typedef bool (RETRO_CALLCONV *retro_calendar_get_current_time_t)(
      struct retro_calendar_time *time);

/**
 * The time zone of the clock.
 */
struct retro_calendar_zone
{
   /**
    * Seconds east of UTC in standard time, DST excluded. DST is in effect
    * at a given time when that time's \c utc_offset differs from this.
    */
   int32_t standard_offset;

   /**
    * IANA time zone name such as "Europe/London", NUL-terminated.
    * Empty if the frontend does not know it.
    */
   char name[64];
};

/**
 * Gets the time zone of the clock.
 *
 * Like the start time, it does not change while the content stays loaded.
 * Cores that emulate
 * a time zone setting use it and keep what they use in their savestates;
 * cores that only need local time use \c utc_offset.
 *
 * @param[out] zone Filled on success.
 * @return \c false if the zone is unknown; \c true with an empty \c name
 * when only \c standard_offset is known.
 */
typedef bool (RETRO_CALLCONV *retro_calendar_get_zone_t)(
      struct retro_calendar_zone *zone);

/**
 * @see RETRO_ENVIRONMENT_GET_CALENDAR_TIME_INTERFACE
 */
struct retro_calendar_time_interface
{
   /**
    * Set by the core to the highest version it supports, and by the
    * frontend to the version it provides. Zero is invalid.
    */
   unsigned interface_version;

   /** @copydoc retro_calendar_get_start_time_t */
   retro_calendar_get_start_time_t get_start_time;

   /** @copydoc retro_calendar_get_current_time_t */
   retro_calendar_get_current_time_t get_current_time;

   /** @copydoc retro_calendar_get_zone_t */
   retro_calendar_get_zone_t get_zone;
};

#endif
