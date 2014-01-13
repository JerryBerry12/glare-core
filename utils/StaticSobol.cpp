/*=====================================================================
StaticSobol.cpp
---------------
Copyright Glare Technologies Limited 2011 -
Generated at Mon Jul 30 23:42:17 +0100 2012
=====================================================================*/
#include "StaticSobol.h"


#include "Exception.h"
#include "fileutils.h"
#include <vector>
#include <fstream>


StaticSobol::StaticSobol(const std::string& indigo_base_dir_path)
{
	std::vector<uint32> direction_nums(418214);
	{
		const std::string dir_data_path = FileUtils::join(indigo_base_dir_path, "data/dirdata.bin");
		std::ifstream dir_file(FileUtils::convertUTF8ToFStreamPath(dir_data_path).c_str(), std::ios::in | std::ios::binary);
		if(!dir_file)
			throw Indigo::Exception("Could not open direction number data file");

		dir_file.read((char *)&direction_nums[0], 418214 * sizeof(uint32));
	}
	std::vector<uint32>::const_iterator dir_iter = direction_nums.begin();


	// Allocate temporary memory used during direction number computation
	uint32 **V = new uint32*[num_dims];
	for(uint32 i = 0; i < num_dims; ++i)
		V[i] = new uint32[32 + 1];


	// Compute direction numbers V[1] to V[L], scaled by pow(2,32)
	for(uint32 i = 1; i <= 32; i++)
		V[0][i] = 1 << (32 - i); // all m's = 1

	// ----- Read V for other dimensions -----
	std::vector<uint32> m(1024);
	for(uint32 j = 1; j <= num_dims - 1; j++)
	{
		// Read in parameters from file 
		const uint32 d = *dir_iter++, s = *dir_iter++, a = *dir_iter++;

		for(uint32 i = 1; i <= s; i++)
			m[i] = *dir_iter++;
	    
		// Compute direction numbers V[1] to V[L], scaled by pow(2,32)
		if(32 <= s)
		{
			for(uint32 i = 1; i <= 32; i++)
				V[j][i] = m[i] << (32 - i);
		}
		else
		{
			for(uint32 i = 1; i <= s; i++)
				V[j][i] = m[i] << (32 - i);

			for(uint32 i = s + 1; i <= 32; i++)
			{
				V[j][i] = V[j][i - s] ^ (V[j][i - s] >> s);

				for(uint32 k = 1; k <= s - 1; k++)
					V[j][i] ^= (((a >> (s - 1 - k)) & 1) * V[j][i - k]);
			}
		}
	}

	// "Flatten" the direction numbers into an aligned 1D array, suitable for SSE access
	// This also removes the extra array dimension (32 instead of 33)
	dir_nums.resize(num_dims * 32);
	for(uint32 d = 0; d < num_dims; ++d)
	for(uint32 z = 0; z < 32; ++z)
		dir_nums[d * 32 + z] = V[d][z + 1];

	// Free temporary memory used in intialising the Sobol direction numbers
	for(uint32 j = 0; j < num_dims; ++j)
		delete[] V[j];
	delete[] V;


	// Initialise the xor-table
	for(uint32 c = 0; c < 16; ++c)
	{
		const uint32 i[4] = { -(c & 1), -((c >> 1) & 1), -((c >> 2) & 1), -(c >> 3) };

		xor_LUT[c] = _mm_set_ps(floatCast(i[3]), floatCast(i[2]), floatCast(i[1]), floatCast(i[0]));
	}
}


StaticSobol::~StaticSobol()
{

}


inline uint32 StaticSobol::evalSampleRef(const uint32 sample_num, const uint32 d) const
{
	assert(d < num_dims);
	uint32 x_d = 0;

	uint32_t k = (uint32_t)sample_num;
	for(uint32 j = 0; k; k >>= 1, j++)
		if(k & 1)
			x_d ^= dir_nums[d * 32 + j];

	return x_d;
}


inline uint32 StaticSobol::evalSampleRefRandomised(const uint64 sample_num, const uint32 d) const
{
	const uint32 sample_num_32 = (uint32)sample_num;			// Get Sobol index from low 32bits of sample number
	const uint32 random_index  = (uint32)(sample_num >> 32);	// Get randomisation vector index from high 32bits of sample number

	// Note that there is expected uint32 wraparound in adding the randomisation vector
	return evalSampleRef(sample_num_32, d) + static_random(random_index * 21211 + d);
}


#if BUILD_TESTS


#include "stringutils.h"
#include "../indigo/TestUtils.h"

void StaticSobol::test(const std::string& indigo_base_dir_path)
{
	///std::cout << "StaticSobol::test()" << std::endl;

	StaticSobol sobol(indigo_base_dir_path);


	// Test that we get the desired Sobol sequence
	{
		const uint32 desired_Sobol[2] = { 0, 1 << 31 }; // The first two Sobol samples are 0 and 0.5 (in fixed point) in all dimensions

		for(uint32 s = 0; s < 2; ++s)
		{
			for(uint32 d = 0; d < num_dims; ++d)
			{
				const uint32 x_d = sobol.evalSampleRef(s, d);
				const uint32 x_d_SSE = sobol.evalSample(s, d);

				testAssert(x_d == desired_Sobol[s]);
				testAssert(x_d_SSE == desired_Sobol[s]);
			}

		}
	}


	// Print out the first few randomised Sobol samples
	for(uint32 s = 0; s < 8; ++s)
	{
		//std::cout << "Randomised Sobol sample " << s << ": ";

		for(uint32 d = 0; d < 5; ++d)
		{
			//const double fixed2float = 1.0 / 4294967296.0;
			//const double x_d_float = sobol.evalSampleRandomised(s, d) * fixed2float;

			//std::cout << doubleToStringNDecimalPlaces(x_d_float, 5) << " ";
		}

		//std::cout << std::endl;
	}	
}

#endif
