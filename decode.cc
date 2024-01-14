/*
Cauchy Reed Solomon Erasure Coding

Copyright 2024 Ahmet Inan <inan@aicodix.de>
*/

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include "crc.hh"
#include "galois_field.hh"
#include "cauchy_reed_solomon_erasure_coding.hh"

int main(int argc, char **argv)
{
	if (argc < 3) {
		std::cerr << "usage: " << argv[0] << " OUTPUT CHUNKS.." << std::endl;
		return 1;
	}
	int chunk_count = argc - 2;
	typedef CODE::GaloisField<16, 0b10001000000001011, uint16_t> GF;
	GF::value_type *chunk_ident = nullptr;
	uint8_t *chunk_data = nullptr;
#ifdef __AVX2__
	int SIMD = 32;
#else
	int SIMD = 16;
#endif
	int SIMD2 = SIMD * sizeof(uint16_t);
	int block_bytes = 0;
	int block_count = 0;
	int block_index = 0;
	int dirty_bytes = 0;
	int output_bytes = 0;
	uint32_t crc32_value = 0;
	bool first = true;
	for (int i = 0; i < chunk_count; ++i) {
		const char *chunk_name = argv[2+i];
		std::ifstream chunk_file(chunk_name, std::ios::binary);
		if (chunk_file.bad()) {
			std::cerr << "Couldn't open file \"" << chunk_name << "\" for reading." << std::endl;
			return 1;
		}
		char magic[3];
		chunk_file.read(magic, 3);
		uint16_t splits;
		chunk_file.read(reinterpret_cast<char *>(&splits), 2);
		uint16_t ident;
		chunk_file.read(reinterpret_cast<char *>(&ident), 2);
		int32_t size = 0;
		chunk_file.read(reinterpret_cast<char *>(&size), 3);
		uint32_t crc32;
		chunk_file.read(reinterpret_cast<char *>(&crc32), 4);
		if (!chunk_file || magic[0] != 'C' || magic[1] != 'R' || magic[2] != 'S' || splits >= 1024 || ident <= splits) {
			std::cerr << "Skipping file \"" << chunk_name << "\"." << std::endl;
			continue;
		}
		if (first) {
			first = false;
			block_count = splits + 1;
			output_bytes = size + 1;
			crc32_value = crc32;
			chunk_ident = new GF::value_type[block_count];
			dirty_bytes = (output_bytes + block_count - 1) / block_count;
			block_bytes = dirty_bytes;
			if (block_bytes % SIMD2)
				block_bytes += SIMD2 - block_bytes % SIMD2;
			int code_bytes = block_count * block_bytes;
			chunk_data = reinterpret_cast<uint8_t *>(std::aligned_alloc(SIMD, code_bytes));
		} else if (block_count != splits + 1 || output_bytes != size + 1 || crc32_value != crc32) {
			std::cerr << "Skipping file \"" << chunk_name << "\"." << std::endl;
			continue;
		}
		chunk_ident[block_index] = ident;
		chunk_file.read(reinterpret_cast<char *>(chunk_data + block_index * block_bytes), dirty_bytes + (dirty_bytes & 1));
		if (++block_index >= block_count)
			break;
	}
	if (block_index != block_count) {
		std::cerr << "Need " << block_count << " valid chunks but only got " << block_index << "." << std::endl;
		return 1;
	}
	const char *output_name = argv[1];
	if (output_name[0] == '-' && output_name[1] == 0)
		output_name = "/dev/stdout";
	std::ofstream output_file(output_name, std::ios::binary | std::ios::trunc);
	if (output_file.bad()) {
		std::cerr << "Couldn't open file \"" << output_name << "\" for writing." << std::endl;
		return 1;
	}
	uint8_t *output_data = reinterpret_cast<uint8_t *>(std::aligned_alloc(SIMD, block_bytes));
	static GF instance;
	CODE::CauchyReedSolomonErasureCoding<GF> crs;
	static CODE::CRC<uint32_t> crc(0x8F6E37A0);
	for (int i = 0, j = 0; i < block_count; ++i) {
		crs.decode(output_data, chunk_data, chunk_ident, i, block_bytes, block_count);
		j += dirty_bytes;
		int copy_bytes = dirty_bytes;
		if (j > output_bytes)
			copy_bytes -= j - output_bytes;
		output_file.write(reinterpret_cast<char *>(output_data), copy_bytes);
		for (int k = 0; k < copy_bytes; ++k)
			crc(output_data[k]);
	}
	delete[] chunk_ident;
	std::free(output_data);
	std::free(chunk_data);
	if (crc() != crc32_value) {
		std::cerr << "CRC value does not match!" << std::endl;
		return 1;
	}
	return 0;
}

