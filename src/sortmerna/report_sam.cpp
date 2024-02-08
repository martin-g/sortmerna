/*
 @copyright 2016-2021  Clarity Genomics BVBA
 @copyright 2012-2016  Bonsai Bioinformatics Research Group
 @copyright 2014-2016  Knight Lab, Department of Pediatrics, UCSD, La Jolla

 @parblock
 SortMeRNA - next-generation reads filter for metatranscriptomic or total RNA
 This is a free software: you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 SortMeRNA is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public License
 along with SortMeRNA. If not, see <http://www.gnu.org/licenses/>.
 @endparblock

 @contributors Jenya Kopylova   jenya.kopylov@gmail.com
			   Laurent No�      laurent.noe@lifl.fr
			   Pierre Pericard  pierre.pericard@lifl.fr
			   Daniel McDonald  wasade@gmail.com
			   Mika�l Salson    mikael.salson@lifl.fr
			   H�l�ne Touzet    helene.touzet@lifl.fr
			   Rob Knight       robknight@ucsd.edu
*/

#include "report_sam.h"
#include "common.hpp"
#include "options.hpp"
#include "read.hpp"
#include "references.hpp"
#include "refstats.hpp"
#include "readfeed.hpp"

ReportSam::ReportSam(Runopts& opts) : Report(opts) {}

ReportSam::ReportSam(Readfeed& readfeed, Runopts& opts) : ReportSam(opts)
{
	init(readfeed, opts);
}

void ReportSam::init(Readfeed& readfeed, Runopts& opts)
{
	fv.resize(readfeed.num_splits);
	fsv.resize(readfeed.num_splits);
	is_zip = (opts.zip_out == 1) || (readfeed.orig_files[0].isZip && opts.zip_out == -1);
	// WORKDIR/out/aligned_0_PID.sam
	for (uint32_t i = 0; i < readfeed.num_splits; ++i) {
		std::string sfx1 = "_" + std::to_string(i);
		std::string sfx2 = opts.is_pid ? "_" + pid_str : "";
		std::string gz = is_zip ? ".gz" : "";
		fv[i] = opts.aligned_pfx.string() + sfx1 + sfx2 + ext + gz;
		openfw(i);
	}
	if (is_zip) init_zip();
}

void ReportSam::append(const uint32_t& id, Read& read, References& refs, Runopts& opts)
{
	std::stringstream ss;
	if (read.is03) read.flip34();

	// read did not align, output null string
	if (opts.is_print_all_reads && read.alignment.alignv.size() == 0)
	{
		// (1) Query
		ss << read.getSeqId();
		ss << "\t4\t*\t0\t0\t*\t*\t0\t0\t*\t*\n";
		return;
	}

	// read aligned, output full alignment
	// iterate read alignments
	for (auto const& align: read.alignment.alignv)
	{
		if (align.index_num == refs.num
			&& align.part == refs.part)
		{
			// (1) Query
			ss << read.getSeqId();
			// (2) flag Forward/Reversed
			if (!align.strand) ss << "\t16\t";
			else ss << "\t0\t";
			// (3) Subject
			ss << refs.buffer[align.ref_num].id;
			// (4) Ref start
			ss << "\t" << align.ref_begin1 + 1;
			// (5) mapq
			ss << "\t" << 255 << "\t";
			// (6) CIGAR
			// output the masked region at beginning of alignment
			if (align.read_begin1 != 0)
				ss << align.read_begin1 << "S";

			for (uint32_t c = 0; c < align.cigar.size(); ++c)
			{
				uint32_t letter = 0xf & align.cigar[c];
				uint32_t length = (0xfffffff0 & align.cigar[c]) >> 4;
				ss << length;
				if (letter == 0) ss << "M";
				else if (letter == 1) ss << "I";
				else ss << "D";
			}

			auto end_mask = read.sequence.size() - align.read_end1 - 1;
			// output the masked region at end of alignment
			if (end_mask > 0) ss << end_mask << "S";
			// (7) RNEXT, (8) PNEXT, (9) TLEN
			ss << "\t*\t0\t0\t";
			// (10) SEQ

			if (align.strand == read.reversed) // XNOR
				read.revIntStr();
			ss << read.get04alphaSeq();
			// (11) QUAL
			ss << "\t";
			// reverse-complement strand
			if (read.quality.size() > 0 && !align.strand)
			{
				std::reverse(read.quality.begin(), read.quality.end());
				ss << read.quality;
			}
			else if (read.quality.size() > 0) // forward strand
			{
				ss << read.quality;
				// FASTA read
			}
			else ss << "*";

			// (12) OPTIONAL FIELD: SW alignment score generated by aligner
			ss << "\tAS:i:" << align.score1;
			// (13) OPTIONAL FIELD: edit distance to the reference
			auto miss_gap_mat = read.calc_miss_gap_match(refs, align);
			ss << "\tNM:i:" << std::get<0>(miss_gap_mat) + std::get<1>(miss_gap_mat) << "\n";
		}
	} // ~for read.alignments
	if (is_zip) {
		auto ret = vzlib_out[id].defstr(ss.str(), fsv[id]); // Z_STREAM_END | Z_OK - ok
		if (ret < Z_OK || ret > Z_STREAM_END) {
			ERR("Failed deflating readstring: ", ss.str(), " zlib status: ", ret);
		}
	}
	else {
		fsv[id] << ss.str();
	}
} // ~ReportSam::append

void ReportSam::write_header(Runopts& opts)
{
	std::stringstream ss;
	ss << "@HD\tVN:1.0\tSO:unsorted\n";

	// TODO: this line is taken from "Index::load_stats". To be finished (20171215).
	for (uint16_t index_num = 0; index_num < (uint16_t)opts.indexfiles.size(); index_num++)
	{
		std::ifstream stats(opts.indexfiles[index_num].second + ".stats", std::ios::in | std::ios::binary);

		// Note: This is simply seeking forward a variable number of bytes
		// I did multiple seeks just to make this code legible, the compiler
		// should consolidate this into a few big ::seekg() calls
		stats.seekg(sizeof(size_t), stats.cur); // filesize
		uint32_t fasta_len;
		stats.read(reinterpret_cast<char*>(&fasta_len), sizeof(uint32_t));
		stats.seekg(sizeof(char)*fasta_len, stats.cur); //variable fasta file name
		stats.seekg(sizeof(double)*4, stats.cur); //background_freq
		stats.seekg(sizeof(uint64_t), stats.cur); //full_len
		stats.seekg(sizeof(uint32_t), stats.cur); //seed_win_len
		stats.seekg(sizeof(uint64_t), stats.cur); //num_seq
		int16_t part_num=0;
		stats.read(reinterpret_cast<char*>(&part_num), sizeof(uint16_t)); //part_num
		stats.seekg(sizeof(index_parts_stats)*part_num, stats.cur); //part_num*index_parts_stats


		// Ok, now that we're done seeking to where we need to be in index stats,
		// we can begin reading the info necessary for our SQ lines:
		uint32_t num_sq = 0;
		stats.read(reinterpret_cast<char*>(&num_sq), sizeof(uint32_t));

		for (uint32_t j = 0; j < num_sq; j++)
		{
			// get the length of the sequence id
			uint32_t len_id = 0;
			stats.read(reinterpret_cast<char*>(&len_id), sizeof(uint32_t));
			// get the sequence id string
			std::string s(len_id, 0);
			stats.read(reinterpret_cast<char*>(&s[0]), sizeof(char)*len_id);
			// get the length of the sequence itself
			uint32_t len_seq = 0;
			stats.read(reinterpret_cast<char*>(&len_seq), sizeof(uint32_t));

			// print to the SAM file:
			if (opts.is_SQ) ss << "@SQ\tSN:" << s << "\tLN:" << len_seq << "\n";
		}

	}
	ss << "@PG\tID:sortmerna\tVN:1.0\tCL:" << opts.cmdline << std::endl;
	if (is_zip) {
		auto ret = vzlib_out[0].defstr(ss.str(), fsv[0]); // Z_STREAM_END | Z_OK - ok
		if (ret < Z_OK || ret > Z_STREAM_END) {
			ERR("Failed deflating readstring: ", ss.str(), " zlib status: ", ret);
		}
	}
	else
		fsv[0] << ss.str();
} // ~ReportSam::write_header