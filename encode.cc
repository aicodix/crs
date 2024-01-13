/*
Cauchy Reed Solomon Erasure Coding

Copyright 2024 Ahmet Inan <inan@aicodix.de>
*/

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include "crc.hh"
#include "galois_field.hh"
#include "cauchy_reed_solomon_erasure_coding.hh"

int main(int argc, char **argv)
{
	if (argc < 4) {
		std::cerr << "usage: " << argv[0] << " INPUT SIZE CHUNKS.." << std::endl;
		return 1;
	}
	const char *input_name = argv[1];
	struct stat sb;
	if (stat(input_name, &sb) < 0 || sb.st_size < 1) {
		std::cerr << "Couldn't get size of file \"" << input_name << "\"." << std::endl;
		return 1;
	}
	if (sb.st_size > 16777216) {
		std::cerr << "Size of file \"" << input_name << "\" too large." << std::endl;
		return 1;
	}
	int input_bytes = sb.st_size;
	int chunk_bytes = std::atoi(argv[2]);
	int chunk_count = argc - 3;
	int crs_overhead = 3 + 1 + 2 + 3 + 4; // CRS SPLITS IDENT SIZE CRC32
	int avail_bytes = (chunk_bytes - crs_overhead) & ~1;
	if (avail_bytes > 65536) {
		std::cerr << "Size of chunks too large." << std::endl;
		return 1;
	}
	int block_count = (input_bytes + avail_bytes - 1) / avail_bytes;
	if (avail_bytes < 1 || block_count > 256) {
		std::cerr << "Size of chunks too small." << std::endl;
		return 1;
	}
	if (chunk_count < block_count) {
		std::cerr << "Need at least " << block_count << " chunks." << std::endl;
		return 1;
	}
	std::cerr << "CRS(" << chunk_count << ", " << block_count << ")" << std::endl;
	std::ifstream input_file(input_name, std::ios::binary);
	if (input_file.bad()) {
		std::cerr << "Couldn't open file \"" << input_name << "\" for reading." << std::endl;
		return 1;
	}
#ifdef __AVX2__
	int SIMD = 32;
#else
	int SIMD = 16;
#endif
	int SIMD2 = SIMD * sizeof(uint16_t);
	int dirty_bytes = (input_bytes + block_count - 1) / block_count;
	int block_bytes = dirty_bytes;
	if (block_bytes % SIMD2)
		block_bytes += SIMD2 - (block_bytes % SIMD2);
	int mesg_bytes = block_count * block_bytes;
	uint8_t *input_data = reinterpret_cast<uint8_t *>(std::aligned_alloc(SIMD, mesg_bytes));
	static CODE::CRC<uint32_t> crc(0x8F6E37A0);
	for (int i = 0, j = 0; i < block_count; ++i) {
		j += dirty_bytes;
		int copy_bytes = dirty_bytes;
		if (j > input_bytes)
			copy_bytes -= j - input_bytes;
		input_file.read(reinterpret_cast<char *>(input_data + block_bytes * i), copy_bytes);
		for (int k = 0; k < copy_bytes; ++k)
			crc(input_data[block_bytes * i + k]);
	}
	typedef CODE::GaloisField<16, 0b10001000000001011, uint16_t> GF;
	static GF instance;
	CODE::CauchyReedSolomonErasureCoding<GF> crs;
	uint8_t *chunk_data = reinterpret_cast<uint8_t *>(std::aligned_alloc(SIMD, block_bytes));
	for (int i = 0; i < chunk_count; ++i) {
		int chunk_ident = block_count + i;
		crs.encode(input_data, chunk_data, chunk_ident, block_bytes, block_count);
		const char *chunk_name = argv[3+i];
		std::ofstream chunk_file(chunk_name, std::ios::binary | std::ios::trunc);
		if (chunk_file.bad()) {
			std::cerr << "Couldn't open file \"" << chunk_name << "\" for writing." << std::endl;
			return 1;
		}
		chunk_file.write("CRS", 3);
		uint8_t splits = block_count - 1;
		chunk_file.write(reinterpret_cast<char *>(&splits), 1);
		uint16_t ident = chunk_ident;
		chunk_file.write(reinterpret_cast<char *>(&ident), 2);
		int32_t size = input_bytes - 1;
		chunk_file.write(reinterpret_cast<char *>(&size), 3);
		uint32_t crc32 = crc();
		chunk_file.write(reinterpret_cast<char *>(&crc32), 4);
		chunk_file.write(reinterpret_cast<char *>(chunk_data), dirty_bytes + (dirty_bytes & 1));
	}
	std::free(input_data);
	std::free(chunk_data);
	return 0;
}

