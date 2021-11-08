/*
   Copyright 2013-2021 Skytechnology sp. z o.o.

   This file is part of LizardFS.

   LizardFS is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, version 3.

   LizardFS is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with LizardFS. If not, see <http://www.gnu.org/licenses/>.
*/

#include <cmath>
#include <fstream>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <regex>
#include <thread>
#include <utility>
#include <vector>


struct Options {
	long chunkSize;
	long chunksMaxCount;
	std::string source;
	std::string destination;
	bool showProgress;
	long querySize;
	double timeoutSeconds;
	long waitingTimeMilliseconds;
	std::ostream &statsStream;

	static Options *getInstance() {
		if (nullptr == instance) {
			instance = new Options();
		}

		return instance;
	}

	Options(Options const &) = delete;

	Options(Options &&) = delete;

protected:
	static Options *instance;

	Options()
			: chunkSize(64 * 1 << 20 /* 64MiB */), chunksMaxCount(-1),
			  source("/dev/urandom"), destination("/dev/null"), showProgress(true),
			  querySize(-1), timeoutSeconds(-1), waitingTimeMilliseconds(0), statsStream(std::cout) {
	}
};

Options *Options::instance = nullptr;


std::mutex fileMutex;

void processOptions() {
	Options *options = Options::getInstance();
	if (-1 == options->querySize) {
		options->querySize = INT64_MAX;
	}
	if (options->chunksMaxCount == -1) {
		options->chunksMaxCount = options->querySize / options->chunkSize;
	}
	if (-1 == options->timeoutSeconds) {
		options->timeoutSeconds = MAXFLOAT;
	}
}

void showUsage(const char *arg0) {
	std::cerr << "Description: Utility to run parameterized I/O operations on a file or device.\n"
				 "\n"
				 "Usage: " << arg0 << " [options]\n"
									  "Options:\n"
									  "\t-c :\tChunk size (${CHUNK_SIZE}).\n"
									  "\t-h :\tShow this help.\n"
									  "\t-i :\tSource to read from. If \"-\" is given it reads from standard input (${SOURCE}).\n"
									  "\t-n :\tNumber of chunks to read (${COUNT}).\n"
									  "\t-P :\tDo not show progress.\n"
									  "\t-s :\tSize to read (${REQUEST_SIZE}).\n"
									  "\t-t :\tTimeout in seconds (${TIMEOUT_S}).\n"
									  "\t-w :\tWaiting time in milliseconds (${WAITING_TIME_MS}).\n";
}

struct HumanReadable {
	double size{};
	std::string suffix;

	explicit HumanReadable(double size_, std::string suffix_ = "") : size{size_}, suffix{std::move(suffix_)} {};
private:
	friend
	std::ostream &operator<<(std::ostream &os, const HumanReadable& hr) {
		long order = (hr.size > 0) ? (long) (log2(hr.size) / 10) : 0;
		os << std::fixed << std::setprecision(2)
		   << hr.size / (1 << (order * 10)) << "BKMGTPE"[order];
		return order == 0 ? os : os << "iB" << hr.suffix
									<< " (" << std::setprecision(0) << hr.size << ')';
	}
};

double readNumber(const std::string &number) {
	char *endPtr;
	return strtod(number.c_str(), &endPtr);
}

long dehumanize(const std::string &number) {
	long result;
	result = long(readNumber(number));
	std::smatch sm;

	if (regex_match(number.begin(), number.end(), std::regex(".*(K|KiB)$"))) {
		result *= 1 << 10;
	} else if (regex_match(number.begin(), number.end(), std::regex(".*(M|MiB)$"))) {
		result *= 1 << 20;
	} else if (regex_match(number.begin(), number.end(), std::regex(",*(G|GiB)$"))) {
		result *= 1 << 30;
	}

	return result;
}

#define TO_NANOSECONDS(duration) std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count()

bool fileRead() {
	using Clock = std::chrono::high_resolution_clock;
	using TimePoint = Clock::time_point;

	Options *options = Options::getInstance();
	bool result = false;
	std::vector<char> buffer(options->chunkSize);
	std::ifstream ifs(options->source, std::ifstream::binary);

	if (ifs) {
		long totalRead = 0;
		std::ofstream ofs(options->destination, std::ofstream::binary);
		if (ofs) {
			long chunksCount = 0;
			double totalElapsed = 0;
			double totalReadingElapsed = 0;
			TimePoint start;
			TimePoint grossEnd;
			TimePoint readEnd;
			long bytesRead;

			do {
				if (totalElapsed > options->timeoutSeconds ||
					chunksCount++ > options->chunksMaxCount) {
					result = true;
					break;
				}

				start = Clock::now();
				{
					std::lock_guard<std::mutex> lock(fileMutex);
					ifs.read(buffer.data(), options->chunkSize);
					readEnd = Clock::now();
					bytesRead = ifs.gcount();
				}

				totalRead += bytesRead;
				std::this_thread::sleep_for(std::chrono::milliseconds(options->waitingTimeMilliseconds));
				grossEnd = Clock::now();

				double grossElapsed = double(TO_NANOSECONDS(grossEnd - start)) * 1e-9;
				totalElapsed += grossElapsed;

				double readingElapsed = double(TO_NANOSECONDS(readEnd - start)) * 1e-9;
				totalReadingElapsed += readingElapsed;

				if (options->showProgress) {
					options->statsStream << "\r"
								  << HumanReadable{(double) bytesRead}
								  << " at " << HumanReadable{(double) bytesRead / grossElapsed, "/s"}
								  << " (read: " << HumanReadable{(double) bytesRead / readingElapsed, "/s)"}
								  << std::flush;
				}
			} while (ifs && ofs);

			if (options->showProgress) {
                options->statsStream << "\n"
							  << HumanReadable{(double) totalRead}
							  << " at " << HumanReadable{(double) totalRead / totalElapsed, "/s"}
							  << " (read: " << HumanReadable{(double) totalRead / totalReadingElapsed, "/s)"}
							  << std::endl;
			}

			return result;
		}
	}

	return result;
}

bool parseArguments(int argc, char *argv[]) {
	Options *options = Options::getInstance();
	int opt;

	while ((opt = getopt(argc, argv, ":c:hi:n:Pt:s:w:")) != -1) {
		switch (opt) {
			case 'c':
				options->chunkSize = dehumanize(optarg);
				break;
			case 'h':
				showUsage(argv[0]);
				break;
			case 'i':
				if (!strcmp(optarg, "-")) {
					options->source = "/dev/stdin";
				} else {
					options->source = optarg;
				}
				break;
			case 'n':
				options->chunksMaxCount = long(readNumber(optarg));
				break;
			case 's':
				options->querySize = dehumanize(optarg);
				break;
			case 'P':
				options->showProgress = false;
				break;
			case 't':
				options->timeoutSeconds = readNumber(optarg);
				break;
			case 'w':
				options->waitingTimeMilliseconds = long(readNumber(optarg));
				break;
			case '?':
			case ':':
				showUsage(argv[0]);
				return false;
			default:
				showUsage(argv[0]);
		}
	}

	if (argc == 1 || optind != argc) {
		showUsage(argv[0]);
	}

	return true;
}

int main(int argc, char *argv[]) {
	parseArguments(argc, argv);
	processOptions();

	if (!fileRead())
		return 1;

	return 0;
}
