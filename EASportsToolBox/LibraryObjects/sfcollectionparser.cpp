// EA Skate SF Collection Parser By GHFear.
#pragma once
#include "refpack/refpackd.h"
#include "tools.h"
#include "EASportsToolBox.h"

namespace EASportsToolBox 
{
  namespace SFCollection
  {

    // StreamFile Decompression Type 1 (Refpack Compressed Single File)
    bool sf_decompress_type1(char* stream_file_bytearray, const char* filename,
      CollectionAsset streamfile_header, const wchar_t* directory, uint32_t sf_header_size)
    {
      std::wstring Filedirectory = directory;
      Filedirectory += EABig::Tools::Strings::ConvertCharToWchar(filename);

      // Declare local variables.
      FILE* file = nullptr;

      // Open the file in write mode to overwrite or create an empty file
      if (!IoTools::create_file(file, Filedirectory))
      {
        return false;
      }

      // Decompress inbuffer to get decompressed out buffer and size.
      std::vector<uint8_t> decompression_in_buffer_vector(
        stream_file_bytearray + sf_header_size, (stream_file_bytearray + sf_header_size) + streamfile_header.Size);
      std::vector<uint8_t> decompression_out_buffer_vector = refpack::decompress(decompression_in_buffer_vector);

      // Write (append) to file.
      if (!IoTools::append_file(file, Filedirectory, (char*)decompression_out_buffer_vector.data(), decompression_out_buffer_vector.size()))
      {
        return false;
      }

      return true;
    }

    // StreamFile Decompression Type 2 (Refpack Compressed Arena Header and/or Body)
    bool sf_decompress_type2_loop(FILE* File, DWORD IsCompressed, DWORD CompressedSize,
      DWORD DecompressedSize, const char* Filename, char* StreamFileBuffer, uint32_t* StreamIndex, const wchar_t* Directory)
    {
      std::wstring file_directory = Directory;
      file_directory += EABig::Tools::Strings::ConvertCharToWchar(Filename);

      if (IsCompressed == 0)
      {
        char* current_file_location = StreamFileBuffer + *StreamIndex;

        // Write (append) to file.
        if (!IoTools::append_file(File, file_directory, (char*)current_file_location, DecompressedSize))
        {
          return false;
        }

        return true;
      }
      else if (IsCompressed == 1)
      {
        // Decompress inbuffer to get decompressed out buffer and size.
        std::vector<uint8_t> decompression_in_buffer_vector(StreamFileBuffer + *StreamIndex, StreamFileBuffer + *StreamIndex + CompressedSize);
        std::vector<uint8_t> decompression_out_buffer_vector = refpack::decompress(decompression_in_buffer_vector);

        if (decompression_out_buffer_vector.size() != DecompressedSize) // Check if actual decompressed size is the same as the expect decompressed size.
        {
          return false;
        }

        // Write (append) to file.
        if (!IoTools::append_file(File, file_directory, (char*)decompression_out_buffer_vector.data(), decompression_out_buffer_vector.size()))
        {
          return false;
        }

        return true;
      }
      else
      {
        return false;
      }
    }

    // StreamFile Decompression Type 2 (Refpack Compressed Arena Header and/or Body)
    bool sf_decompress_type2(char* StreamFileBuffer, const char* Filename, const wchar_t* Directory, uint32_t StreamFileHeaderSize)
    {
      // Declare local variables.
      SFCompressionType2 compressionheader = {};
      uint32_t compression_header_size = sizeof(SFCompressionType2);
      uint32_t stream_index = 0;
      uint32_t* stream_index_ptr = &stream_index;
      std::wstring file_directory = Directory;
      file_directory += EABig::Tools::Strings::ConvertCharToWchar(Filename);

      // Fill compressionheader struct with important information from the SF compression header.
      std::memcpy(&compressionheader, StreamFileBuffer + StreamFileHeaderSize + 0, compression_header_size);
      compressionheader.BodyCompressedBool = EABig::Tools::BigToLittleUINT(compressionheader.BodyCompressedBool);
      compressionheader.BodyCompressedSize = EABig::Tools::BigToLittleUINT(compressionheader.BodyCompressedSize);
      compressionheader.BodyDecompressedSize = EABig::Tools::BigToLittleUINT(compressionheader.BodyDecompressedSize);
      compressionheader.HeaderCompressedBool = EABig::Tools::BigToLittleUINT(compressionheader.HeaderCompressedBool);
      compressionheader.HeaderCompressedSize = EABig::Tools::BigToLittleUINT(compressionheader.HeaderCompressedSize);
      compressionheader.HeaderDecompressedSize = EABig::Tools::BigToLittleUINT(compressionheader.HeaderDecompressedSize);
      compressionheader.Unknown1 = EABig::Tools::BigToLittleUINT(compressionheader.Unknown1);
      compressionheader.Unknown3 = EABig::Tools::BigToLittleUINT(compressionheader.Unknown3);

      *stream_index_ptr = StreamFileHeaderSize + compression_header_size;

      // Open the file in write mode to overwrite or create an empty file
      FILE* file = nullptr;
      if (!IoTools::create_file(file, file_directory))
      {
        return false;
      }

      if (compressionheader.HeaderCompressedSize != 0 || compressionheader.HeaderDecompressedSize != 0)
      {
        if (!sf_decompress_type2_loop(
          file,
          compressionheader.HeaderCompressedBool,
          compressionheader.HeaderCompressedSize,
          compressionheader.HeaderDecompressedSize,
          Filename,
          StreamFileBuffer,
          stream_index_ptr,
          Directory))
        {
          return false;
        }
      }

      stream_index += compressionheader.HeaderCompressedSize; // Move stream index forward the size of the first (arena header) chunk.

      if (compressionheader.BodyCompressedSize != 0 || compressionheader.BodyDecompressedSize != 0)
      {
        if (!sf_decompress_type2_loop(
          file,
          compressionheader.BodyCompressedBool,
          compressionheader.BodyCompressedSize,
          compressionheader.BodyDecompressedSize,
          Filename,
          StreamFileBuffer,
          stream_index_ptr,
          Directory))
        {
          return false;
        }
      }

      return true;
    }

    // StreamFile Decompression Type 3 (Arena Chunks Individually Compressed With Refpack)
    bool sf_decompress_type3(char* stream_file_bytearray, const char* filename, const wchar_t* directory, uint32_t sf_header_size)
    {
      std::wstring Filedirectory = directory;
      Filedirectory += EABig::Tools::Strings::ConvertCharToWchar(filename);

      // Declare local variables.
      SFCompressionType3 compressionheader;
      uint64_t stream_index = 0;
      FILE* file = nullptr;

      // Fill compressionheader struct with important information from the SF compression header.
      std::memcpy(&compressionheader, stream_file_bytearray + sf_header_size, 32);
      compressionheader.CompressionHeaderSize = EABig::Tools::BigToLittleUINT(compressionheader.CompressionHeaderSize);
      compressionheader.AmountCompressedChunks = EABig::Tools::BigToLittleUINT(compressionheader.AmountCompressedChunks);
      compressionheader.CompressionBool = EABig::Tools::BigToLittleUINT(compressionheader.CompressionBool);
      compressionheader.Unknown1 = EABig::Tools::BigToLittleUINT(compressionheader.Unknown1);
      compressionheader.Unknown2 = EABig::Tools::BigToLittleUINT(compressionheader.Unknown2);
      compressionheader.Unknown3 = EABig::Tools::BigToLittleUINT(compressionheader.Unknown3);
      compressionheader.Unknown4 = EABig::Tools::BigToLittleUINT(compressionheader.Unknown4);
      compressionheader.Unknown5 = EABig::Tools::BigToLittleUINT(compressionheader.Unknown5);

      stream_index = (uint64_t)sf_header_size + 32;

      // Create the CompressedBlockSizes vector.
      for (size_t i = 0; i < (compressionheader.CompressionHeaderSize - 32) / 4; i++)
      {
        DWORD TempCompressedBlockSize = 0;
        std::memcpy(&TempCompressedBlockSize, stream_file_bytearray + stream_index, sizeof(DWORD));
        TempCompressedBlockSize = EABig::Tools::BigToLittleUINT(TempCompressedBlockSize);
        compressionheader.CompressedBlockSizes.push_back(TempCompressedBlockSize);
        stream_index += 4;
      }

      // Open the file in write mode to overwrite or create an empty file
      if (!IoTools::create_file(file, Filedirectory))
      {
        return false;
      }

      // Unpack loop
      for (size_t i = 0; i < compressionheader.AmountCompressedChunks; i++)
      {
        // Create decompression in-buffer.
        uint64_t decompression_in_buffer_size = compressionheader.CompressedBlockSizes[i];

        // Decompress inbuffer to get decompressed out buffer and size.
        std::vector<uint8_t> decompression_in_buffer_vector(stream_file_bytearray
          + stream_index, (stream_file_bytearray + stream_index) + decompression_in_buffer_size);
        std::vector<uint8_t> decompression_out_buffer_vector = refpack::decompress(decompression_in_buffer_vector);

        // Write (append) to file.
        if (!IoTools::append_file(file, Filedirectory,
          (char*)decompression_out_buffer_vector.data(), decompression_out_buffer_vector.size()))
        {
          return false;
        }

        // Move one compressed chunk forward
        stream_index += decompression_in_buffer_size;
      }

      return true;
    }

    // Function to unpack files from a sf archive. (Doesn't decompress)
    ParserResult ParseSFCollection(std::wstring InPath, bool bUnpack, int64_t SelectedIndexToUnpack) {

      // Setup return type
      ParserResult RESULT = {};

      // Declare local variables.
      FILE* archive = nullptr;
      long sf_collection_size = 0;
      long start_of_sf_collection = 0;
      uint32_t size_of_sfa_header = 0;
      std::wstring streamfilename_directory = EABig::Tools::Strings::GetFilenameWithoutExtension(InPath.c_str());
      std::wstring out_directory = EABig::Tools::Strings::ParseFilePath(InPath).first + streamfilename_directory + L"\\";

      _wfopen_s(&archive, InPath.c_str(), L"rb");
      if (archive == NULL) {
        perror("Error opening archive");
        return RESULT;
      }

      // Save start position.
      start_of_sf_collection = ftell(archive);

      // Save archive size and go back to start position.
      fseek(archive, 0, SEEK_END);
      sf_collection_size = ftell(archive);
      fseek(archive, start_of_sf_collection, SEEK_SET);

      // Save archive headersize and go back to start position.
      fseek(archive, 16, SEEK_CUR);
      fread(&size_of_sfa_header, sizeof(DWORD), 1, archive);
      size_of_sfa_header = EABig::Tools::BigToLittleUINT(size_of_sfa_header);
      fseek(archive, start_of_sf_collection, SEEK_SET);

      // Read SF Archive header.
      SFArchiveHeader* sfa_header = (SFArchiveHeader*)malloc(sizeof(*sfa_header) + (size_of_sfa_header - 32));
      if (sfa_header == NULL) {
        fclose(archive);
        free(sfa_header);
        return RESULT;
      }
      fread(sfa_header, size_of_sfa_header, 1, archive);
      sfa_header->NumCollections = EABig::Tools::BigToLittleUINT(sfa_header->NumCollections);
      sfa_header->Offset = EABig::Tools::BigToLittleUINT(sfa_header->Offset);
      sfa_header->Stamp = EABig::Tools::BigToLittleUINT(sfa_header->Stamp);
      sfa_header->StreamFormat = EABig::Tools::BigToLittleUINT(sfa_header->StreamFormat);
      sfa_header->Unknown4 = EABig::Tools::BigToLittleUINT(sfa_header->Unknown4);
      sfa_header->Version = EABig::Tools::BigToLittleUINT(sfa_header->Version);

      if (bUnpack) { // Attempt to create the directory if we are trying to unpack.
        if (EABig::Tools::CreateDirectoryRecursively(out_directory.c_str())) {
          wprintf(L"Directory created: %s\n", out_directory.c_str());
        }
        else {
          wprintf(L"Failed to create directory or directory already exists: %s\n", out_directory.c_str());
        }
      }

      // Main unpacking loop.
      uint64_t loop_index = 0;
      while (ftell(archive) < sf_collection_size)
      {
        ParserIndexResult ParserIndex = {};

        // Declare local variables.
        char char_filename[17];
        std::string filename = "";
        uint32_t streamfile_header_size = 0;
        long streamfile_startposition = 0;
        long stream_file_size = 0;

        // Create our stream_file_header struct and read the header into it from our file.
        streamfile_startposition = ftell(archive);

        // Save headersize and go back to start position.
        fseek(archive, 12, SEEK_CUR);
        fread(&streamfile_header_size, sizeof(uint32_t), 1, archive);
        fseek(archive, streamfile_startposition, SEEK_SET);

        // Read SF Header.
        streamfile_header_size = EABig::Tools::BigToLittleUINT(streamfile_header_size);
        CollectionAsset* stream_file_header = (CollectionAsset*)malloc(sizeof(*stream_file_header) + (streamfile_header_size - 20));
        if (stream_file_header == NULL) {
          fclose(archive);
          free(sfa_header);
          free(stream_file_header);
          return RESULT;
        }
        fread(stream_file_header, streamfile_header_size, 1, archive);
        stream_file_header->Stride = EABig::Tools::BigToLittleUINT(stream_file_header->Stride);
        stream_file_header->Offset = EABig::Tools::BigToLittleUINT(stream_file_header->Offset);
        stream_file_header->Size = EABig::Tools::BigToLittleUINT(stream_file_header->Size);

        // Check for invalid values.
        if (
          stream_file_header->Offset > sf_collection_size
          || stream_file_header->Size > sf_collection_size
          || stream_file_header->Stride > sf_collection_size
          || stream_file_header->Size > stream_file_header->Stride
          )
        {
          free(sfa_header);
          free(stream_file_header);
          return RESULT;
        }

        // Set stream file block size
        stream_file_size = stream_file_header->Stride;

        // Set the filename
        EABig::Tools::Strings::bytearray_to_hexstring(stream_file_header->ID, sizeof(stream_file_header->ID), char_filename);
        filename = char_filename;
        const wchar_t* file_extension = EABig::Tools::Strings::GetFileExtension(InPath.c_str());

        // Add to parse struct vector
        ParserIndex.Offset = streamfile_startposition;
        ParserIndex.Size = stream_file_size;
        ParserIndex.FileName = filename;

        if (std::wstring(file_extension) == L"psf")
        {
          filename += ".psg";
          ParserIndex.FileExtension = ".psg";
        }
        else if (std::wstring(file_extension) == L"xsf")
        {
          filename += ".rx2";
          ParserIndex.FileExtension = ".rx2";
        }
        else if (std::wstring(file_extension) == L"wsf")
        {
          filename += ".rg2";
          ParserIndex.FileExtension = ".rg2";
        }

        bool full_archive_unpack = bUnpack && SelectedIndexToUnpack == -1; // Do a full archive unpack.
        bool single_archive_unpack = bUnpack && SelectedIndexToUnpack == loop_index; // Do a single file archive unpack.

        // Run this code if we set unpack to true when calling this function.
        if (full_archive_unpack || single_archive_unpack)
        {
          // Create byte array with size of file_size
          char* stream_file_buffer = (char*)malloc(stream_file_size);
          if (stream_file_buffer == NULL) {
            perror("Error allocating memory");
            fclose(archive);
            free(sfa_header);
            free(stream_file_header);
            free(stream_file_buffer);
            return RESULT;
          }

          // Read current streamfile into a bytearray.
          fseek(archive, streamfile_startposition, SEEK_SET);
          fread(stream_file_buffer, sizeof(char), stream_file_size, archive);
          
          switch (sfa_header->StreamFormat)
          {
          case SF1:
            if (!sf_decompress_type1(stream_file_buffer, filename.c_str(),
              *stream_file_header, out_directory.c_str(), stream_file_header->Offset))
            {
              MessageBox(
                0, L"Error writing data! \nMake sure you don't have a handle to a file from some other tool! \nUnpacker is unable to continue!",
                L"Unpacker Prompt", MB_OK | MB_ICONINFORMATION);
              free(stream_file_header);
              free(sfa_header);
              fclose(archive);
              return RESULT;
            }
            break;
          case SF2:
            if (!sf_decompress_type2(stream_file_buffer, filename.c_str(),
              out_directory.c_str(), stream_file_header->Offset))
            {
              MessageBox(
                0, L"Error writing data! \nMake sure you don't have a handle to a file from some other tool! \nUnpacker is unable to continue!",
                L"Unpacker Prompt", MB_OK | MB_ICONINFORMATION);
              free(stream_file_header);
              free(sfa_header);
              fclose(archive);
              return RESULT;
            }
            break;
          case SF3:
            if (!sf_decompress_type3(stream_file_buffer, filename.c_str(),
              out_directory.c_str(), stream_file_header->Offset))
            {
              MessageBox(
                0, L"Error writing data! \nMake sure you don't have a handle to a file from some other tool! \nUnpacker is unable to continue!",
                L"Unpacker Prompt", MB_OK | MB_ICONINFORMATION);
              free(stream_file_header);
              free(sfa_header);
              fclose(archive);
              return RESULT;
            }
            break;
          case UNKNOWN:
            break;
          default:
            break;
          }

          free(stream_file_buffer);
        }

        // free byte array memory and seek to next streamfile start position. 
        RESULT.ParserData.push_back(ParserIndex);
        free(stream_file_header);
        fseek(archive, streamfile_startposition + stream_file_size, SEEK_SET);
        loop_index++;
      }

      // Close the archive.
      fclose(archive);
      free(sfa_header);
      RESULT.bSuccess = true;
      return RESULT;
    }

    // Function to parse the SFA Header (Doesn't decompress)
    SFArchiveHeaderNotDynamic parse_sfa_header(const wchar_t* archiveName) {
      // Declare local variables.
      FILE* archive = nullptr;
      long sfa_size = 0;
      long start_of_sfa = 0;
      SFArchiveHeaderNotDynamic sfa_header = {};

      _wfopen_s(&archive, archiveName, L"rb");
      if (archive == NULL) {
        perror("Error opening archive");
        return sfa_header;
      }

      // Save start position.
      start_of_sfa = ftell(archive);

      // Save filesize and go back to start position.
      fseek(archive, 0, SEEK_END);
      sfa_size = ftell(archive);
      fseek(archive, start_of_sfa, SEEK_SET);

      // Save headersize and go back to start position.
      fseek(archive, 16, SEEK_CUR);
      uint32_t size_of_sfa_header = 0;
      fread(&size_of_sfa_header, sizeof(DWORD), 1, archive);

      size_of_sfa_header = EABig::Tools::BigToLittleUINT(size_of_sfa_header);
      fseek(archive, start_of_sfa, SEEK_SET);

      // Read SFA header.
      fread(&sfa_header, sizeof(sfa_header), 1, archive);
      sfa_header.NumCollections = EABig::Tools::BigToLittleUINT(sfa_header.NumCollections);
      sfa_header.Offset = EABig::Tools::BigToLittleUINT(sfa_header.Offset);
      sfa_header.Stamp = EABig::Tools::BigToLittleUINT(sfa_header.Stamp);
      sfa_header.StreamFormat = EABig::Tools::BigToLittleUINT(sfa_header.StreamFormat);
      sfa_header.Unknown4 = EABig::Tools::BigToLittleUINT(sfa_header.Unknown4);
      sfa_header.Version = EABig::Tools::BigToLittleUINT(sfa_header.Version);

      // Close the archive and free memory.
      fclose(archive);

      return sfa_header;
    }

  } // End of SFCollection Namespace

} // End of EASportsToolsBox Namespace


