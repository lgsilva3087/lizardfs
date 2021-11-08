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

#include <iostream>
#include <fstream>
#include <regex>
#include <cmath>
#include <thread>
#include <iomanip>
#include <mutex>
#include <vector>

#include <getopt.h>

struct Options {
	long chunkSize;
	long chunksMaxCount;
	std::string source;
	std::string destination;
	bool showProgress;
	long querySize;
	double timeoutSeconds;
	long waitingTimeMilliseconds;

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
		  querySize(-1), timeoutSeconds(-1), waitingTimeMilliseconds(0) {
	}
};

Options *Options::instance = nullptr;

struct Stats {
	std::ostream &stream;

	static Stats *getInstance(std::ostream &stream) {
		if (nullptr == instance) {
			instance = new Stats(stream);
		}
		return instance;
	}

private:
	static Stats *instance;

	explicit Stats(std::ostream &stream_) : stream(stream_) {};

	Stats(Stats const &, std::ostream &stream) : stream(stream) {};

	Stats(Stats &&, std::ostream &stream) : stream(stream) {};

	virtual ~Stats() {};
};

Stats *Stats::instance = nullptr;

std::mutex fileMutex;

void processOptions(const long& fileSize) {
	Options *options = Options::getInstance();
	if (-1 == options->querySize) {
		options->querySize = fileSize;
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
									  "\t-j :\tNumber of jobs to run (${JOBS}).\n"
									  "\t-n :\tNumber of chunks to read (${COUNT}).\n"
									  "\t-o :\tDestination to write to. If \"-\" is given it writes to standard output (${DESTINATION}).\n"
									  "\t-P :\tDo not show progress.\n"
									  "\t-s :\tSize to read (${REQUEST_SIZE}).\n"
									  "\t-t :\tTimeout in seconds (${TIMEOUT_S}).\n"
									  "\t-w :\tWaiting time in milliseconds (${WAITING_TIME_MS}).\n";

}

std::string humanize(const double& size) {
	std::vector<std::string> units {"B", "KiB", "MiB", "GiB", "TiB", "PiB",
									"EiB", "ZiB", "YiB"};
	long order = (size > 0) ? (long) (log2(size) / 10) : 0;
	std::stringstream stringBuilder;
	stringBuilder << std::fixed << std::setprecision(2)
				  << size / (1 << (order * 10)) << " "
				  << units[order];

	return stringBuilder.str();
}

double readNumber(const std::string &number) {
	char *endPtr;
	return strtod(number.c_str(), &endPtr);
}

long dehumanize(const std::string& number) {
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

long fileLength(std::ifstream &fs) {
	std::lock_guard<std::mutex> lock(fileMutex);
	long currentPos = fs.tellg();
	fs.seekg(0, std::ifstream::end);
	long length = fs.tellg();
	fs.seekg(currentPos, std::ifstream::beg);

	return length;
}

#define TO_NANOSECONDS(duration) std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count()

bool fileCopy(std::ostream &statsOutput) {
	using Clock = std::chrono::high_resolution_clock;
	using TimePoint = Clock::time_point;

	Options *options = Options::getInstance();
	Stats *stats = Stats::getInstance(statsOutput);
	bool result = false;
	std::vector<char> buffer(options->chunkSize);
	std::ifstream ifs(options->source, std::ifstream::binary);

	if (ifs) {
		long fileSize = fileLength(ifs);
		processOptions(fileSize);
		long totalRead = 0;
		std::ofstream ofs(options->destination, std::ofstream::binary);
		if (ofs) {
			long offset = 0;
			long chunksCount = 0;
			double totalElapsed = 0;
			double totalReadingElapsed = 0;
			double totalWritingElapsed = 0;
			TimePoint start;
			TimePoint writeEnd;
			TimePoint grossEnd;
			TimePoint readEnd;
			long bytesRead;

			do {
				if (totalElapsed > options->timeoutSeconds ||
					chunksCount++ > options->chunksMaxCount) {
					break;
				}

				start = Clock::now();
				{
					std::lock_guard<std::mutex> lock(fileMutex);
					{
						ifs.seekg(offset, std::ifstream::beg);
						ifs.read(buffer.data(), options->chunkSize);
						readEnd = Clock::now();
						bytesRead = ifs.gcount();
					}
					{
						ofs.seekp(offset, std::ofstream::beg);
						ofs.write(buffer.data(), bytesRead);
						ofs.flush();
					}
				}

				writeEnd = Clock::now();
				totalRead += bytesRead;
				offset += bytesRead;
				std::this_thread::sleep_for(std::chrono::milliseconds(options->waitingTimeMilliseconds));
				grossEnd = Clock::now();

				double grossElapsed = double(TO_NANOSECONDS(grossEnd - start)) * 1e-9;
				totalElapsed += grossElapsed;

				double readingElapsed = double(TO_NANOSECONDS(readEnd - start)) * 1e-9;
				totalReadingElapsed += readingElapsed;

				double writingElapsed = double(TO_NANOSECONDS(writeEnd - readEnd)) * 1e-9;
				totalWritingElapsed += writingElapsed;

				if (options->showProgress) {
					stats->stream << "\r"
								  << humanize((double) bytesRead)
								  << " at " << humanize((double) bytesRead / grossElapsed) << "/s "
								  << "( read: " << humanize((double) bytesRead / readingElapsed) << "/s, "
								  << "write: " << humanize((double) bytesRead / writingElapsed) << "/s )"
								  << std::flush;
				}
			} while (ifs && ofs);

			if (options->showProgress) {
				stats->stream << "\n"
							  << humanize((double) totalRead)
							  << " at " << humanize((double) totalRead / totalElapsed) << "/s "
							  << "( read: " << humanize((double) totalRead / totalReadingElapsed) << "/s, "
							  << "write: " << humanize((double) totalRead / totalWritingElapsed) << "/s )"
							  << std::endl;
			}

			return fileSize == totalRead;
		}
	}

	return result;
}

bool parseArguments(int argc, char *argv[]) {
	Options *options = Options::getInstance();
	int opt;

	while ((opt = getopt(argc, argv, ":c:hi:j:n:o:Pt:s:w:")) != -1) {
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
			case 'j':
				readNumber(optarg);
				break;
			case 'n':
				options->chunksMaxCount = long(readNumber(optarg));
				break;
			case 'o':
				if (!strcmp(optarg, "-")) {
					options->destination = "/dev/stdout";
				} else {
					options->destination = optarg;
				}
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

	if (!fileCopy(std::cout))
		return 1;

	return 0;
}
