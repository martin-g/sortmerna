/**
* FILE: readstats.hpp
* Created: Nov 17, 2017 Fri
* @copyright 2016-19 Clarity Genomics BVBA
*
* Read file statistics
*/

// standard
#include <chrono>
#include <algorithm> // remove_if
#include <iomanip> // output formatting
#include <locale> // isspace
#include <sstream>
#include <fstream> // std::ifstream
#include <iostream> // std::cout
#include <cstring> // memcpy
#include <ios>

// 3rd party
#include "zlib.h"
#include "kseq_load.hpp"

// SMR
#include "readstats.hpp"
#include "kvdb.hpp"
#include "gzip.hpp"

const std::string Readstats::dbkey = "Readstats";

Readstats::Readstats(Runopts & opts)
	:
	opts(opts),
	min_read_len(READLEN),
	max_read_len(0),
	total_reads_mapped(0),
	total_reads_mapped_cov(0),
	number_total_read(0),
	full_file_size(0),
	full_read_main(0),
	reads_matched_per_db(opts.indexfiles.size(), 0),
	total_reads_denovo_clustering(0),
	stats_calc_done(false)
{
	calcSuffix();
	if (!opts.exit_early)
		calculate(); // number_total_read only
}

void Readstats::calculate()
{
	std::stringstream ss;
	uint64_t tcount = 0;

	std::ifstream ifs(opts.readfiles[0], std::ios_base::in | std::ios_base::binary);
	if (!ifs.is_open()) {
		ss << "Failed to open Reads file: " << opts.readfiles[0];
		ERR(ss.str());
		exit(EXIT_FAILURE);
	}
	else
	{
		std::string line; // line from the Reads file
		std::string sequence; // full sequence of a Read (can contain multiple lines for Fasta files)
		bool isFastq = false;
		bool isFasta = false;
		Gzip gzip(opts.is_gz);

		auto t = std::chrono::high_resolution_clock::now();

		std::cout << "Readstats::calculate starting ...   ";

		for (int count = 0, stat = 0; ; ++count) // count of lines in a Single record
		{
			stat = gzip.getline(ifs, line);
			++tcount;

			if (stat == RL_END)
			{
				if (!sequence.empty())
				{
					// process the last record
					++number_total_read;
					full_read_main += sequence.length();
				}
				break;
			}

			if (stat == RL_ERR)
			{
				ss.str("");
				ss << STAMP << " ERROR reading from Reads file. Exiting...";
				ERR(ss.str());
				exit(EXIT_FAILURE);
			}

			if (line.empty()) 
			{
				--count;
				--tcount;
				continue; // skip empty line
			}
			
			// right-trim whitespace in place (removes '\r' too)
			line.erase(std::find_if(line.rbegin(), line.rend(), [l = std::locale{}](auto ch) { return !std::isspace(ch, l); }).base(), line.end());
			//line.erase(std::remove_if(begin(line), end(line), [l = std::locale{}](auto ch) { return std::isspace(ch, l); }), end(line));

			if (tcount == 1)
			{
				isFastq = (line[0] == FASTQ_HEADER_START);
				isFasta = (line[0] == FASTA_HEADER_START);

				if (!(isFasta || isFastq))
				{
					ss.str("");
					ss << STAMP << " the line [" << line << "] is not FASTA/Q header";
					ERR(ss.str());
					exit(EXIT_FAILURE);
				}
			}

			if (count == 4 && isFastq)
			{
				count = 0;
				if (line[0] != FASTQ_HEADER_START)
				{
					ss.str("");
					ss << STAMP << " the line [" << line << "] is not FASTQ header. number_total_read= "
						<< number_total_read << " tcount= " << tcount;
					ERR(ss.str());
					exit(EXIT_FAILURE);
				}
			}

			// fastq: 0(header), 1(seq), 2(+), 3(quality)
			// fasta: 0(header), 1(seq)
			if ((isFasta && line[0] == FASTA_HEADER_START) || (count == 0 && isFastq))
			{
				if (!sequence.empty())
				{ // process previous sequence
					++number_total_read;
					full_read_main += sequence.length();
				}

				count = 0; // FASTA record start
				sequence.clear(); // clear container for the new record
			} // ~if header line
			else 
			{
				if (isFastq)
				{
					if (count > 3)
					{
						ss.str("");
						ss << STAMP << " Unexpected number of lines : " << count 
							<< " in a single FASTQ Read. Total reads processed: " << number_total_read
							<< " Last sequence: " << sequence
							<< " Last line read: " << line
							<< " Exiting...";
						ERR(ss.str()); 
						exit(EXIT_FAILURE);
					}
					if ( count == 3 || line[0] == '+' ) 
						continue; // fastq.quality
				} // ~if fastq

				sequence += line; // fasta multiline sequence
			}
		} // ~for getline

		std::chrono::duration<double> elapsed = std::chrono::high_resolution_clock::now() - t;
		ss << std::setprecision(2) << std::fixed 
			<< "Readstats::calculate done. Elapsed time: " << elapsed.count() 
			<< " sec. Reads processed: " << number_total_read << std::endl;
		std::cout << ss.str();
	}
	ifs.close();
} // ~Readstats::calculate

// determine the suffix (fasta, fastq, ...) of aligned strings
void Readstats::calcSuffix()
{
	const std::string suff = opts.readfiles[0].substr(opts.readfiles[0].rfind('.') + 1);
	if (suff.length() > 0 && !opts.is_gz)
		suffix.assign(suff);
	else if (filesig == FASTA_HEADER_START)
		suffix.assign("fasta");
	else
		suffix.assign("fastq");
}

std::string Readstats::toString()
{
	std::string buf;
	std::copy_n(static_cast<char*>(static_cast<void*>(&min_read_len)), sizeof(min_read_len), std::back_inserter(buf));
	std::copy_n(static_cast<char*>(static_cast<void*>(&max_read_len)), sizeof(max_read_len), std::back_inserter(buf));
	auto val = total_reads_mapped.load();
	std::copy_n(static_cast<char*>(static_cast<void*>(&val)), sizeof(val), std::back_inserter(buf));
	val = total_reads_mapped_cov.load();
	std::copy_n(static_cast<char*>(static_cast<void*>(&val)), sizeof(val), std::back_inserter(buf));
	std::copy_n(static_cast<char*>(static_cast<void*>(&total_reads_denovo_clustering)), sizeof(total_reads_denovo_clustering), std::back_inserter(buf));
	std::copy_n(static_cast<char*>(static_cast<void*>(&stats_calc_done)), sizeof(stats_calc_done), std::back_inserter(buf));

	// vector reads_matched_per_db
	size_t reads_matched_per_db_size = reads_matched_per_db.size();
	std::copy_n(static_cast<char*>(static_cast<void*>(&reads_matched_per_db_size)), sizeof(reads_matched_per_db_size), std::back_inserter(buf));
	for (auto entry: reads_matched_per_db)
		std::copy_n(static_cast<char*>(static_cast<void*>(&entry)), sizeof(entry), std::back_inserter(buf));

	return buf;
}

bool Readstats::restoreFromDb(KeyValueDatabase & kvdb)
{
	bool ret = false;
	std::string bstr = kvdb.get(Readstats::dbkey);
	if (bstr.size() == 0) { return ret; }
	size_t offset = 0;
	std::stringstream ss;

	std::memcpy(static_cast<void*>(&min_read_len), bstr.data() + offset, sizeof(min_read_len));
	offset += sizeof(min_read_len);

	std::memcpy(static_cast<void*>(&max_read_len), bstr.data() + offset, sizeof(max_read_len));
	offset += sizeof(max_read_len);

	uint64_t val = 0;
	std::memcpy(static_cast<void*>(&val), bstr.data() + offset, sizeof(val));
	total_reads_mapped = val;
	offset += sizeof(val);

	val = 0;
	std::memcpy(static_cast<void*>(&val), bstr.data() + offset, sizeof(val));
	total_reads_mapped_cov = val;
	offset += sizeof(val);

	std::memcpy(static_cast<void*>(&total_reads_denovo_clustering), bstr.data() + offset, sizeof(total_reads_denovo_clustering));
	offset += sizeof(total_reads_denovo_clustering);

	std::memcpy(static_cast<void*>(&stats_calc_done), bstr.data() + offset, sizeof(stats_calc_done));
	offset += sizeof(stats_calc_done);

	size_t reads_matched_per_db_size = 0;
	std::memcpy(static_cast<void*>(&reads_matched_per_db_size), bstr.data() + offset, sizeof(reads_matched_per_db_size));
	offset += sizeof(reads_matched_per_db_size);
	if (reads_matched_per_db_size == reads_matched_per_db.size()) 
	{
		for (std::vector<uint64_t>::iterator it = reads_matched_per_db.begin(); it != reads_matched_per_db.end(); ++it)
		{
			std::memcpy(static_cast<void*>(&*it), bstr.data() + offset, sizeof(uint64_t));
			offset += sizeof(uint64_t);
		}
		ret = true;
	}
	else
	{
		ss << "Readstats::restoreFromDb: reads_matched_per_db.size stored in DB: " << reads_matched_per_db_size 
			<< " doesn't match the number of reference files: "	<< reads_matched_per_db.size() << std::endl;
		std::cout << ss.str(); ss.str("");
		ret = false;
	}

	return ret;
} // ~Readstats::restoreFromDb

void Readstats::pushOtuMap(std::string & ref_seq_str, std::string & read_seq_str)
{
	//std::lock_guard<std::mutex> omlg(otu_map_lock);
	otu_map[ref_seq_str].push_back(read_seq_str);
}

void Readstats::printOtuMap(std::string otumapfile)
{
	std::stringstream ss;
	std::ofstream omstrm;
	omstrm.open(otumapfile);

	ss << __FILE__ << ":" << __LINE__ << " Printing OTU Map.." << std::endl;
	std::cout << ss.str(); ss.str("");

	for (std::map<std::string, std::vector<std::string>>::iterator it = otu_map.begin(); it != otu_map.end(); ++it)
	{
		omstrm << it->first << "\t";
		int i = 0;
		for (std::vector<std::string>::iterator itv = it->second.begin(); itv != it->second.end(); ++itv)
		{
			if (i < it->second.size() - 1)
				omstrm << *itv << "\t";
			else
				omstrm << *itv; // last element
			++i;
		}
		omstrm << std::endl;
	}
	if (omstrm.is_open()) omstrm.close();
}