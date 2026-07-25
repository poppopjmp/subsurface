// SPDX-License-Identifier: GPL-2.0
#include <string.h>
#include <stdlib.h>

#include "bounded-reader.h"
#include "divesite.h"
#include "dive.h"
#include "divelog.h"
#include "errorhelper.h"
#include "gettext.h"
#include "subsurface-string.h"
#include "file.h"
#include "sample.h"

// Convert bytes into an INT
#define array_uint16_le(p) ((unsigned int) (p)[0] \
							+ ((p)[1]<<8) )
#define array_uint32_le(p) ((unsigned int) (p)[0] \
							+ ((p)[1]<<8) + ((p)[2]<<16) \
							+ ((p)[3]<<24))

struct lv_event {
	uint32_t time;
	struct pressure {
		int sensor;
		int mbar;
	} pressure;
};

// Liquivision supports the following sensor configurations:
// Primary sensor only
// Primary + Buddy sensor
// Primary + Up to 4 additional sensors
// Primary + Up to 9 addiitonal sensors
struct lv_sensor_ids {
	uint16_t primary;
	uint16_t buddy;
	uint16_t group[9];
};

struct lv_sensor_ids sensor_ids;

static int handle_event_ver2(int, const unsigned char *, unsigned int, struct lv_event *)
{
	// Skip 4 bytes
	return 4;
}


static int handle_event_ver3(int code, const unsigned char *ps, unsigned int ps_ptr, struct lv_event *event)
{
	int skip = 4;
	uint16_t current_sensor;

	switch (code) {
	case 0x0002:	//	Unknown
	case 0x0004:	//	Unknown
		skip = 4;
		break;
	case 0x0005:	// Unknown
		skip = 6;
		break;
	case 0x0007:	// Gas
		// 4 byte time
		// 1 byte O2, 1 bye He
		skip = 6;
		break;
	case 0x0008:
		// 4 byte time
		// 2 byte gas setpoint 2
		skip = 6;
		break;
	case 0x000f:
		// Tank pressure
		event->time = array_uint32_le(ps + ps_ptr);
		current_sensor = array_uint16_le(ps + ps_ptr + 4);

		event->pressure.sensor = -1;
		event->pressure.mbar = array_uint16_le(ps + ps_ptr + 6) * 10; // cb->mb

		if (current_sensor == sensor_ids.primary) {
			event->pressure.sensor = 0;
		} else if (current_sensor == sensor_ids.buddy) {
			event->pressure.sensor = 1;
		} else {
			int i;
			for (i = 0; i < 9; ++i) {
				if (current_sensor == sensor_ids.group[i]) {
					event->pressure.sensor = i + 2;
					break;
				}
			}
		}

		// 1 byte PSR
		// 1 byte ST
		skip = 10;
		break;
	case 0x0010: {
		// 4 byte time
		// 2 byte primary transmitter S/N
		// 2 byte buddy transmitter S/N
		// 2 byte group transmitter S/N (9x)

		// I don't think it's possible to change sensor IDs once a dive has started but disallow it here just in case
		if (sensor_ids.primary == 0) {
			sensor_ids.primary = array_uint16_le(ps + ps_ptr + 4);
		}

		if (sensor_ids.buddy == 0) {
			sensor_ids.buddy = array_uint16_le(ps + ps_ptr + 6);
		}

		int i;
		const unsigned char *group_ptr = ps + ps_ptr + 8;
		for (i = 0; i < 9; ++i, group_ptr += 2) {
			if (sensor_ids.group[i] == 0) {
				sensor_ids.group[i] = array_uint16_le(group_ptr);
			}
		}

		skip = 26;
		break;
	}
	case 0x0015:	// Unknown
		skip = 2;
		break;
	default:
		skip = 4;
		break;
	}

	return skip;
}

static void parse_dives(int log_version, const unsigned char *buf, unsigned int buf_size, struct dive_table &table, dive_site_table &sites)
{
	unsigned int ptr = 0;
	unsigned char model;

	struct divecomputer *dc;
	struct sample *sample;

	while (ptr < buf_size) {
		int i;
		auto dive = std::make_unique<struct dive>();
		memset(&sensor_ids, 0, sizeof(sensor_ids));
		dc = &dive->dcs[0];

		/* Just the main cylinder until we can handle the buddy cylinder porperly */
		for (i = 0; i < 1; i++) {
			cylinder_t cyl = default_cylinder(dive.get());
			dive->cylinders.add(i, std::move(cyl));
		}

		// Model 0=Xen, 1,2=Xeo, 4=Lynx, other=Liquivision
		model = *(buf + ptr);
		switch (model) {
		case 0:
			dc->model = "Xen";
			break;
		case 1:
		case 2:
			dc->model = "Xeo";
			break;
		case 4:
			dc->model = "Lynx";
			break;
		default:
			dc->model = "Liquivision";
			break;
		}
		ptr++;

		// Dive location, assemble Location and Place.
		// Both lengths are 32 bit fields straight out of the file, and each was
		// used directly as a std::string length - so a crafted file copied up to
		// 4GB from past the end of the buffer into a dive site name the user then
		// sees. Read them through the cursor, which refuses anything that does not
		// fit and latches the failure.
		unsigned int len, place_len;
		std::string location, place;
		BoundedReader rd(buf, buf_size);
		if (!rd.seek(ptr))
			break;

		len = rd.u32_le();
		std::string loc_str = rd.str(len);
		place_len = rd.u32_le();
		place = rd.str(place_len);
		if (!rd.ok()) {
			report_info("DEBUG: truncated dive location - terminating parser");
			break;
		}

		if (!loc_str.empty() && !place.empty())
			location = loc_str + ", " + place;
		else if (!loc_str.empty())
			location = loc_str;
		else
			location = place;

		/* Store the location only if we have one */
		if (!location.empty())
			sites.find_or_create(location)->add_dive(dive.get());

		ptr = rd.pos();

		// Dive comment
		len = array_uint32_le(buf + ptr);
		ptr += 4;
		if (!rd.seek(ptr) || !rd.has(len)) {
			report_info("DEBUG: truncated dive comment - terminating parser");
			break;
		}

		// Blank notes are better than the default text
		std::string notes((char *)buf + ptr, len);
		if (!starts_with(notes, "Comment ..."))
			dive->notes = std::move(notes);
		ptr += len;

		// Fixed size part of the dive record: id, number, duration, depths,
		// timestamps, pressures, temperatures, salinity, sample count and the
		// sample interval. 38 bytes, none of which were bounds checked.
		if (ptr + 38 > buf_size) {
			report_info("DEBUG: truncated dive record - terminating parser");
			break;
		}

		dive->id = array_uint32_le(buf + ptr);
		ptr += 4;

		dive->number = array_uint16_le(buf + ptr) + 1;
		ptr += 2;

		dive->duration.seconds = array_uint32_le(buf + ptr);	// seconds
		ptr += 4;

		dive->maxdepth.mm = array_uint16_le(buf + ptr) * 10;	// cm->mm
		ptr += 2;

		dive->meandepth.mm = array_uint16_le(buf + ptr) * 10;	// cm->mm
		ptr += 2;

		dive->when = array_uint32_le(buf + ptr);
		ptr += 4;

		//unsigned int end_time = array_uint32_le(buf + ptr);
		ptr += 4;

		//unsigned int sit = array_uint32_le(buf + ptr);
		ptr += 4;
		//if (sit == 0xffffffff) {
		//}

		dive->surface_pressure.mbar = array_uint16_le(buf + ptr);		// ???
		ptr += 2;

		//unsigned int rep_dive = array_uint16_le(buf + ptr);
		ptr += 2;

		dive->mintemp.mkelvin =  C_to_mkelvin((float)array_uint16_le(buf + ptr)/10);// C->mK
		ptr += 2;

		dive->maxtemp.mkelvin =  C_to_mkelvin((float)array_uint16_le(buf + ptr)/10);// C->mK
		ptr += 2;

		dive->salinity = *(buf + ptr);	// ???
		ptr += 1;

		unsigned int sample_count = array_uint32_le(buf + ptr);
		ptr += 4;

		// Sample interval
		unsigned char sample_interval;
		sample_interval = 1;

		unsigned char intervals[6] = {1,2,5,10,30,60};
		if (*(buf + ptr) < 6)
			sample_interval = intervals[*(buf + ptr)];
		ptr += 1;

		[[maybe_unused]] float start_cns = 0;
		[[maybe_unused]] unsigned char dive_mode = 0;
		[[maybe_unused]] unsigned char algorithm = 0;
		if (ptr + 4 > buf_size) {
			report_info("DEBUG: truncated sample header - terminating parser");
			break;
		}
		if (array_uint32_le(buf + ptr) != sample_count) {
			// Xeo, with CNS and OTU: 3 floats, 2 bytes and another count
			if (ptr + 18 > buf_size) {
				report_info("DEBUG: truncated Xeo record - terminating parser");
				break;
			}
			// memcpy rather than a cast - the buffer is not aligned
			memcpy(&start_cns, buf + ptr, 4);
			ptr += 4;
			float f;
			memcpy(&f, buf + ptr, 4);
			dive->cns = lrintf(f);	// end cns
			ptr += 4;
			memcpy(&f, buf + ptr, 4);
			dive->otu = lrintf(f);
			ptr += 4;
			dive_mode = *(buf + ptr++);	// 0=Deco, 1=Gauge, 2=None, 35=Rec
			algorithm = *(buf + ptr++);	// 0=ZH-L16C+GF
			sample_count = array_uint32_le(buf + ptr);
		}

		if (sample_count == 0) {
			report_info("DEBUG: sample count 0 - terminating parser");
			break;
		}
		// "ptr + sample_count * 4 + 4 > buf_size" was an unsigned multiply, so a
		// sample_count of 0x40000000 wrapped to zero and sailed through the very
		// check that was meant to stop it. Divide instead of multiplying.
		if (sample_count > (buf_size - ptr - 4) / 4) {
			report_info("DEBUG: BOF - terminating parser");
			break;
		}
		// we aren't using the start_cns, dive_mode, and algorithm, yet

		ptr += 4;

		// Parse dive samples
		const unsigned char *ds = buf + ptr;
		const unsigned char *ts = buf + ptr + sample_count * 2 + 4;
		const unsigned char *ps = buf + ptr + sample_count * 4 + 4;
		// ps_count itself is four more bytes past the sample arrays
		size_t ps_offset = ptr + sample_count * 4 + 4;
		if (ps_offset + 4 > buf_size) {
			report_info("DEBUG: truncated event count - terminating parser");
			break;
		}
		unsigned int ps_count = array_uint32_le(ps);
		ps += 4;
		// How much of the buffer the event walker below is allowed to touch.
		// Everything it reads is at "ps + ps_ptr", and ps_ptr advances by
		// amounts that come out of the file itself.
		size_t ps_avail = buf_size - (ps_offset + 4);

		// Bump ptr
		ptr += sample_count * 4 + 4;

		// Handle events
		unsigned int ps_ptr;
		ps_ptr = 0;

		unsigned int event_code, d = 0, e;
		struct lv_event event;
		memset(&event, 0, sizeof(event));

		// Loop through events
		for (e = 0; e < ps_count; e++) {
			// The event handlers below read a payload whose size depends on
			// the event code, all of it at "ps + ps_ptr" and none of it
			// previously checked against the end of the buffer. The largest
			// is the sensor id event (0x0010), which reads through
			// ps_ptr + 25 and skips 26, so require the event code plus 26
			// bytes to be present before dispatching one.
			if (ps_ptr > ps_avail || ps_avail - ps_ptr < 2 + 26) {
				report_info("DEBUG: truncated event stream - terminating events");
				break;
			}
			// Get event
			event_code = array_uint16_le(ps + ps_ptr);
			ps_ptr += 2;

			if (log_version == 3) {
				ps_ptr += handle_event_ver3(event_code, ps, ps_ptr, &event);
				if (event_code != 0xf)
					continue;	// ignore all but pressure sensor event
			} else {	// version 2
				ps_ptr += handle_event_ver2(event_code, ps, ps_ptr, &event);
				continue;		// ignore all events
			}
			uint32_t sample_time, last_time;
			int depth_mm, last_depth, temp_mk, last_temp;

			while (true) {
				sample = prepare_sample(dc);

				// Get sample times
				sample_time = d * sample_interval;
				depth_mm = array_uint16_le(ds + d * 2) * 10; // cm->mm
				temp_mk = C_to_mkelvin((float)array_uint16_le(ts + d * 2) / 10); // dC->mK
				last_time = (d ? (d - 1) * sample_interval : 0);

				if (d == sample_count) {
					// We still have events to record
					sample->time.seconds = event.time;
					sample->depth.mm = array_uint16_le(ds + (d - 1) * 2) * 10; // cm->mm
					sample->temperature.mkelvin = C_to_mkelvin((float) array_uint16_le(ts + (d - 1) * 2) / 10); // dC->mK
					add_sample_pressure(sample, event.pressure.sensor, event.pressure.mbar);

					break;
				} else if (event.time > sample_time) {
					// Record sample and loop
					sample->time.seconds = sample_time;
					sample->depth.mm = depth_mm;
					sample->temperature.mkelvin = temp_mk;
					d++;

					continue;
				} else if (event.time == sample_time) {
					sample->time.seconds = sample_time;
					sample->depth.mm = depth_mm;
					sample->temperature.mkelvin = temp_mk;
					add_sample_pressure(sample, event.pressure.sensor, event.pressure.mbar);
					d++;

					break;
				} else {	// Event is prior to sample
					sample->time.seconds = event.time;
					add_sample_pressure(sample, event.pressure.sensor, event.pressure.mbar);
					if (last_time == sample_time) {
						sample->depth.mm = depth_mm;
						sample->temperature.mkelvin = temp_mk;
					} else {
						// Extrapolate
						last_depth = array_uint16_le(ds + (d - 1) * 2) * 10; // cm->mm
						last_temp = C_to_mkelvin((float) array_uint16_le(ts + (d - 1) * 2) / 10); // dC->mK
						sample->depth.mm = last_depth + (depth_mm - last_depth)
							* ((int)event.time - (int)last_time) / sample_interval;
						sample->temperature.mkelvin = last_temp + (temp_mk - last_temp)
							* ((int)event.time - (int)last_time) / sample_interval;
					}

					break;
				}
			} // while (true);
		} // for each event sample

		// record trailing depth samples
		for ( ;d < sample_count; d++) {
			sample = prepare_sample(dc);
			sample->time.seconds = d * sample_interval;

			sample->depth.mm = array_uint16_le(ds + d * 2) * 10; // cm->mm
			sample->temperature.mkelvin =
				C_to_mkelvin((float)array_uint16_le(ts + d * 2) / 10);
		}

		if (log_version == 3 && model == 4) {
			// Advance to begin of next dive
			switch (array_uint16_le(ps + ps_ptr)) {
			case 0x0000:
				ps_ptr += 5;
				break;
			case 0x0100:
				ps_ptr += 7;
				break;
			case 0x0200:
				ps_ptr += 9;
				break;
			case 0x0300:
				ps_ptr += 11;
				break;
			case 0x0b0b:
				ps_ptr += 27;
				break;
			}

			while (((ptr + ps_ptr + 4) < buf_size) && (*(ps + ps_ptr) != 0x04))
				ps_ptr++;
		}

		// End dive
		table.record_dive(std::move(dive));

		// Advance ptr for next dive
		ptr += ps_ptr + 4;
	} // while
}

int try_to_open_liquivision(const char *, std::string &mem, struct divelog *log)
{
	const unsigned char *buf = (unsigned char *)mem.data();
	int log_version;

	// Every field here is attacker controlled. In particular "len" used to be
	// added to the cursor unchecked, so any value larger than the file made
	// "buf_size - ptr" underflow to nearly 4GB - after which none of the bounds
	// checks inside parse_dives() meant anything either.
	BoundedReader reader(buf, mem.size());

	// Get name length, then ignore the length field and the name
	unsigned int len = reader.u32_le();
	if (!reader.skip(len))
		return report_error("%s", translate("gettextFromC", "Not a Liquivision file: truncated header"));

	unsigned int dive_count = reader.u32_le();
	if (dive_count == 0xffffffff) {
		// File version 3.0
		log_version = 3;
		reader.skip(2);
		dive_count = reader.u32_le();
	} else {
		log_version = 2;
	}
	(void)dive_count;
	if (!reader.ok())
		return report_error("%s", translate("gettextFromC", "Not a Liquivision file: truncated header"));

	parse_dives(log_version, buf + reader.pos(), mem.size() - reader.pos(), log->dives, log->sites);

	return 1;
}
