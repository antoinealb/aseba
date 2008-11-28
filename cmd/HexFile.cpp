/*
	Aseba - an event-based framework for distributed robot control
	Copyright (C) 2006 - 2008:
		Stephane Magnenat <stephane at magnenat dot net>
		(http://stephane.magnenat.net)
		and other contributors, see authors.txt for details
		Mobots group, Laboratory of Robotics Systems, EPFL, Lausanne
	
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	any other version as decided by the two original authors
	Stephane Magnenat and Valentin Longchamp.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
	
	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "HexFile.h"
#include <istream>
#include <ostream>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <cassert>
#include <algorithm>
#include <valarray>

namespace Aseba
{
	std::string HexFile::EarlyEOF::toString() const
	{
		return FormatableString("Early end of file after %0 lines").arg(line);
	}
	
	std::string HexFile::InvalidRecord::toString() const
	{
		return FormatableString("Invalid record at line %0").arg(line);
	}
	
	std::string HexFile::WrongCheckSum::toString() const
	{
		return FormatableString("Wrong checksum (%0 instead of %1) at line %2").arg(computedCheckSum, 0, 16).arg(recordCheckSum, 0, 16).arg(line);
	}
	
	std::string HexFile::UnknownRecordType::toString() const
	{
		return FormatableString("Unknown record type (%1) at line %0").arg(line).arg(recordType, 0, 16);
	}
	
	std::string HexFile::FileOpeningError::toString() const
	{
		return FormatableString("Can't open file %0").arg(fileName);
	}
	
	unsigned HexFile::getUint4(std::istream &stream)
	{
		int c = stream.get();
		if (c <= '9')
			return c - '0';
		else if (c <= 'F')
			return c - 'A' + 10;
		else
			return c - 'a' + 10;
	}
	
	unsigned HexFile::getUint8(std::istream &stream)
	{
		return (getUint4(stream) << 4) | getUint4(stream);
	}
	
	unsigned HexFile::getUint16(std::istream &stream)
	{
		return (getUint8(stream) << 8) | getUint8(stream);
	}
	
	void HexFile::read(const std::string &fileName)
	{
		std::ifstream ifs(fileName.c_str());
		
		if (ifs.bad())
			throw FileOpeningError(fileName);
		
		int lineCounter = 0;
		uint32 baseAddress = 0;
		
		while (ifs.good())
		{
			// leading ":" character
			char c;
			ifs >> c;
			if (c != ':')
				throw InvalidRecord(lineCounter);
			
			uint8 computedCheckSum = 0;
			
			// record data length
			uint8 dataLength = getUint8(ifs);
			computedCheckSum += dataLength;
			
			// short address
			uint16 lowAddress = getUint16(ifs);
			computedCheckSum += lowAddress;
			computedCheckSum += lowAddress >> 8;
			
			// record type
			uint8 recordType = getUint8(ifs);
			computedCheckSum += recordType;
			
			switch (recordType)
			{
				case 0:
				// data record
				{
					// read data
					std::vector<uint8> recordData;
					for (int i = 0; i != dataLength; i++)
					{
						uint8 d = getUint8(ifs);
						computedCheckSum += d;
						recordData.push_back(d);
						//std::cout << "data " << std::hex << (unsigned)d << "\n";
					}
					
					// verify checksum
					uint8 checkSum = getUint8(ifs);
					computedCheckSum = 1 + ~computedCheckSum;
					if (checkSum != computedCheckSum)
						throw WrongCheckSum(lineCounter, checkSum, computedCheckSum);
					
					// compute address
					uint32 address = lowAddress;
					address += baseAddress;
					//std::cout << "data record at address 0x" << std::hex << address << "\n";
					
					// is some place to add
					bool found = false;
					for (ChunkMap::iterator it = data.begin(); it != data.end(); ++it)
					{
						size_t chunkSize = it->second.size();
						if (address == it->first + chunkSize)
						{
							// copy new
							it->second.reserve(chunkSize + recordData.size());
							std::copy(recordData.begin(), recordData.end(), std::back_inserter(it->second));
							found = true;
							//std::cout << "tail fusable chunk found\n";
							break;
						}
						else if (address + recordData.size() == it->first)
						{
							// resize
							it->second.resize(chunkSize + recordData.size());
							// move
							std::copy_backward(it->second.begin(), it->second.begin() + chunkSize, it->second.end());
							// copy new
							std::copy(recordData.begin(), recordData.end(), it->second.begin());
							found = true;
							//std::cout << "head fusable chunk found\n";
							break;
						}
					}
					if (!found)
						data[address] = recordData;
				}
				break;
				
				case 1:
				// end of file record
				for (ChunkMap::iterator it = data.begin(); it != data.end(); ++it)
				{
					//std::cout << "End of file found. Address " << it->first << " size " << it->second.size() << "\n";
				}
				return;
				break;
				
				case 2:
				// extended segment address record
				{
					if (dataLength != 2)
						throw InvalidRecord(lineCounter);
					
					// read data
					uint16 highAddress = getUint16(ifs);
					computedCheckSum += highAddress;
					computedCheckSum += highAddress >> 8;
					baseAddress = highAddress;
					baseAddress <<= 4;
					//std::cout << "Extended segment address record (?!): 0x" << std::hex << baseAddress << "\n";
					
					// verify checksum
					uint8 checkSum = getUint8(ifs);
					computedCheckSum = 1 + ~computedCheckSum;
					if (checkSum != computedCheckSum)
						throw WrongCheckSum(lineCounter, checkSum, computedCheckSum);
				}
				break;
				
				case 4:
				// extended linear address record
				{
					if (dataLength != 2)
						throw InvalidRecord(lineCounter);
					
					// read data
					uint16 highAddress = getUint16(ifs);
					computedCheckSum += highAddress;
					computedCheckSum += highAddress >> 8;
					baseAddress = highAddress;
					baseAddress <<= 16;
					//std::cout << "Linear address record: 0x" << std::hex << baseAddress << "\n";
					
					// verify checksum
					uint8 checkSum = getUint8(ifs);
					computedCheckSum = 1 + ~computedCheckSum;
					if (checkSum != computedCheckSum)
						throw WrongCheckSum(lineCounter, checkSum, computedCheckSum);
				}
				break;
				
				default:
				throw UnknownRecordType(lineCounter, recordType);
				break;
			}
			
			lineCounter++;
		}
		
		throw EarlyEOF(lineCounter);
	}
	
	void HexFile::writeExtendedLinearAddressRecord(std::ofstream &stream, unsigned addr16) const
	{
		assert(addr16 <= 65535);
		
		uint8 checkSum = 0;
		
		stream << ":02000002";
		checkSum += 0x02;
		checkSum += 0x00;
		checkSum += 0x00;
		checkSum += 0x02;
		
		stream << std::setw(2);
		stream << (addr16 & 0xFF);
		checkSum += (addr16 & 0xFF);
		
		stream << std::setw(2);
		stream << (addr16 >> 8);
		checkSum += (addr16 >> 8);
		
		checkSum = (~checkSum) + 1;
		stream << std::setw(2);
		stream << (unsigned)checkSum;
		
		stream << "\n";
	}
	
	void HexFile::writeData(std::ofstream &stream, unsigned addr16, unsigned count8, uint8 *data) const
	{
		assert(addr16 <= 65535);
		assert(count8 <= 255);
		
		uint8 checkSum = 0;
		
		stream << ":";
		
		stream << std::setw(2);
		stream << count8;
		checkSum += count8;
		
		stream << std::setw(2);
		stream << (addr16 & 0xFF);
		checkSum += (addr16 & 0xFF);
		
		stream << std::setw(2);
		stream << (addr16 >> 8);
		checkSum += (addr16 >> 8);
		
		stream << "00";
		checkSum += 0x00;
		
		for (unsigned i = 0; i < count8; i++)
		{
			stream << std::setw(2);
			stream << (unsigned)data[i];
			checkSum += data[i];
		}
		
		checkSum = (~checkSum) + 1;
		stream << std::setw(2);
		stream << (unsigned)checkSum;
		
		stream << "\n";
	}
	
	void HexFile::write(const std::string &fileName) const
	{
		std::ofstream ofs(fileName.c_str());
		
		if (ofs.bad())
			throw FileOpeningError(fileName);
		
		// set format
		ofs.flags(std::ios::hex);
		ofs.fill('0');
		
		// for each chunk
		for (ChunkMap::const_iterator it = data.begin(); it != data.end(); it++)
		{
			// split address
			unsigned address = it->first;
			unsigned amount = it->second.size();
			unsigned highAddress = address >> 16;
			
			writeExtendedLinearAddressRecord(ofs, highAddress);
			for (unsigned count = 0; count < amount;)
			{
				// check if we have changed 64 K boundary, if so, write new high address
				unsigned newHighAddress = (address + count) >> 16;
				if (newHighAddress != highAddress)
					writeExtendedLinearAddressRecord(ofs, newHighAddress);
				highAddress = newHighAddress;
				
				// write data
				unsigned rowCount = std::min(amount - count, (unsigned)32);
				unsigned lowAddress = address & 0xFFFF;
				std::valarray<uint8> buffer(rowCount);
				std::copy(it->second.begin() + count, it->second.begin() + count + rowCount, &buffer[0]);
				writeData(ofs, lowAddress, rowCount , &buffer[0]);
				
				// increment counters
				count += rowCount;
			}
		}
		
		// write EOF
		ofs << ":00000001FF";
	}
}

