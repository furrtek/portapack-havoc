/*
 * Copyright (C) 2015 Jared Boone, ShareBrained Technology, Inc.
 *
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "file.hpp"

#include <algorithm>

/* Values added to FatFs FRESULT enum, values outside the FRESULT data type */
static_assert(sizeof(FIL::err) == 1, "FatFs FIL::err size not expected.");
#define FR_DISK_FULL	(0x100)
#define FR_EOF          (0x101)
#define FR_BAD_SEEK		(0x102)

File::File(
	const std::string& filename,
	openmode mode
) : err { FR_OK }
{
	BYTE fatfs_mode = 0;
	if( mode & openmode::in ) {
		fatfs_mode |= FA_READ;
	}
	if( mode & openmode::out ) {
		fatfs_mode |= FA_WRITE;
	}
	if( mode & openmode::trunc ) {
		fatfs_mode |= FA_CREATE_ALWAYS;
	}
	if( mode & openmode::ate ) {
		fatfs_mode |= FA_OPEN_ALWAYS;
	}

	err = f_open(&f, filename.c_str(), fatfs_mode);
	if( err == FR_OK ) {
		if( mode & openmode::ate ) {
			err = f_lseek(&f, f_size(&f));
			if( err != FR_OK ) {
				f_close(&f);
			}
		}
	}
}

File::~File() {
	f_close(&f);
}

bool File::read(void* const data, const size_t bytes_to_read) {
	if( err != FR_OK ) {
		return false;
	}

	UINT bytes_read = 0;
	err = f_read(&f, data, bytes_to_read, &bytes_read);
	if( bytes_read != bytes_to_read ) {
		err = FR_EOF;
	}
	return (err == FR_OK);
}

bool File::write(const void* const data, const size_t bytes_to_write) {
	if( err != FR_OK ) {
		return false;
	}
	
	UINT bytes_written = 0;
	err = f_write(&f, data, bytes_to_write, &bytes_written);
	if( bytes_written != bytes_to_write ) {
		err = FR_DISK_FULL;
	}
	return (err == FR_OK);
}

uint64_t File::seek(const uint64_t new_position) {
	if( err != FR_OK ) {
		return false;
	}
	
	const auto old_position = f_tell(&f);
	err = f_lseek(&f, new_position);
	if( err != FR_OK ) {
		f_close(&f);
	}
	if( f_tell(&f) != new_position ) {
		err = FR_BAD_SEEK;
		f_close(&f);
	}
	return old_position;
}

bool File::puts(const std::string& string) {
	const auto result = f_puts(string.c_str(), &f);
	if( result != (int)string.size() ) {
		err = FR_DISK_FULL;
	}
	return (result >= 0);
}

bool File::sync() {
	if( err != FR_OK ) {
		return false;
	}

	err = f_sync(&f);
	return (err == FR_OK);
}

static std::string find_last_file_matching_pattern(const std::string& pattern) {
	std::string last_match;
	for(const auto& entry : std::filesystem::directory_iterator("", pattern.c_str())) {
		if( std::filesystem::is_regular_file(entry.status()) ) {
			const auto match = entry.path();
			if( match > last_match ) {
				last_match = match;
			}
		}
	}
	return last_match;
}

static std::string remove_filename_extension(const std::string& filename) {
	const auto extension_index = filename.find_last_of('.');
	return filename.substr(0, extension_index);
}

static std::string increment_filename_stem_ordinal(const std::string& filename_stem) {
	std::string result { filename_stem };

	auto it = result.rbegin();

	// Increment decimal number before the extension.
	for(; it != result.rend(); ++it) {
		const auto c = *it;
		if( c < '0' ) {
			return { };
		} else if( c < '9' ) {
			*it += 1;
			break;
		} else if( c == '9' ) {
			*it = '0';
		} else {
			return { };
		}
	}

	return result;
}

std::string next_filename_stem_matching_pattern(const std::string& filename_stem_pattern) {
	const auto filename = find_last_file_matching_pattern(filename_stem_pattern + ".*");
	auto filename_stem = remove_filename_extension(filename);
	if( filename_stem.empty() ) {
		filename_stem = filename_stem_pattern;
		std::replace(std::begin(filename_stem), std::end(filename_stem), '?', '0');
	} else {
		filename_stem = increment_filename_stem_ordinal(filename_stem);
	}
	return filename_stem;
}

namespace std {
namespace filesystem {

std::string filesystem_error::what() const {
	switch(err) {
	case FR_OK: 					return "";
	case FR_DISK_ERR:				return "disk error";
	case FR_INT_ERR:				return "insanity detected";
	case FR_NOT_READY:				return "not ready";
	case FR_NO_FILE:				return "no file";
	case FR_NO_PATH:				return "no path";
	case FR_INVALID_NAME:			return "invalid name";
	case FR_DENIED:					return "denied";
	case FR_EXIST:					return "exists";
	case FR_INVALID_OBJECT:			return "invalid object";
	case FR_WRITE_PROTECTED:		return "write protected";
	case FR_INVALID_DRIVE:			return "invalid drive";
	case FR_NOT_ENABLED:			return "not enabled";
	case FR_NO_FILESYSTEM:			return "no filesystem";
	case FR_MKFS_ABORTED:			return "mkfs aborted";
	case FR_TIMEOUT:				return "timeout";
	case FR_LOCKED:					return "locked";
	case FR_NOT_ENOUGH_CORE:		return "not enough core";
	case FR_TOO_MANY_OPEN_FILES:	return "too many open files";
	case FR_INVALID_PARAMETER:		return "invalid parameter";
	case FR_EOF:					return "end of file";
	case FR_DISK_FULL:				return "disk full";
	case FR_BAD_SEEK:				return "bad seek";
	default:						return "unknown";
	}
}

directory_iterator::directory_iterator(
	const char* path,
	const char* wild
) {
	impl = std::make_shared<Impl>();
	const auto result = f_findfirst(&impl->dir, &impl->filinfo, path, wild);
	if( result != FR_OK ) {
		impl.reset();
		// TODO: Throw exception if/when I enable exceptions...
	}
}

directory_iterator& directory_iterator::operator++() {
	const auto result = f_findnext(&impl->dir, &impl->filinfo);
	if( (result != FR_OK) || (impl->filinfo.fname[0] == 0) ) {
		impl.reset();
	}
	return *this;
}

bool is_regular_file(const file_status s) {
	return !(s & AM_DIR);
}

space_info space(const path& p) {
	DWORD free_clusters { 0 };
	FATFS* fs;
	if( f_getfree(p.c_str(), &free_clusters, &fs) == FR_OK ) {
#if _MAX_SS != _MIN_SS
		static_assert(false, "FatFs not configured for fixed sector size");
#else
		return {
			(fs->n_fatent - 2) * fs->csize * _MIN_SS,
			free_clusters * fs->csize * _MIN_SS,
			free_clusters * fs->csize * _MIN_SS,
		};
#endif
	} else {
		return { 0, 0, 0 };
	}
}

} /* namespace filesystem */
} /* namespace std */
