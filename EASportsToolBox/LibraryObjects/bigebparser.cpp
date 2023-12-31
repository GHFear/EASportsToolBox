// EA Skate EB Parser By GHFear.

#pragma once
#include "refpack/refpackd.h"
#include "Zlib/Include/zlib-1.3/zlib.h"
#include "lzx/xmem_lzx.h"
#include "tools.h"
#include "EASportsToolBox.h"

namespace EASportsToolBox 
{
  namespace BigEB
  {
    bool unpack_empty_file(FILE* archive, DWORD SIZE, std::wstring Filedirectory, std::wstring Filepath)
    {
      FILE* file = nullptr;

      // Attempt to create the directory
      if (EABig::Tools::CreateDirectoryRecursively(Filedirectory.c_str())) {
        wprintf(L"Directory created: %s\n", Filedirectory.c_str());
      }
      else {
        wprintf(L"Failed to create directory or directory already exists: %s\n", Filedirectory.c_str());
      }

      // Create empty file.
      if (!IoTools::create_file(file, Filepath)) {
        return false;
      }

      return true;
    }

    bool decompress_deflate(const unsigned char* compressedData, size_t compressedSize, unsigned char* decompressedData, size_t& decompressedSize)
    {
      z_stream stream;
      stream.zalloc = Z_NULL;
      stream.zfree = Z_NULL;
      stream.opaque = Z_NULL;
      stream.avail_in = static_cast<uInt>(compressedSize);
      stream.next_in = const_cast<Bytef*>(compressedData);

      if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        std::cerr << "Error initializing zlib inflate for deflate format." << std::endl;
        return false;
      }

      stream.avail_out = static_cast<uInt>(decompressedSize);
      stream.next_out = decompressedData;

      if (inflate(&stream, Z_FINISH) != Z_STREAM_END) {
        std::cerr << "Error in zlib decompression for deflate format." << std::endl;
        inflateEnd(&stream);
        return false;
      }

      decompressedSize = stream.total_out;

      if (inflateEnd(&stream) != Z_OK) {
        std::cerr << "Error ending zlib inflate for deflate format." << std::endl;
        return false;
      }

      return true;
    }

    bool chunk_decompress(FILE* archive, std::wstring Filedirectory, std::wstring Filepath)
    {
      // Local Variables.
      std::vector<uint8_t> decompression_out_buffer_vector = {};
      size_t decompressed_size = 0;

      ChunkPackHeader chunk_pack_header = {};
      fread(&chunk_pack_header, sizeof(chunk_pack_header), 1, archive);
      chunk_pack_header.alignedTo = EABig::Tools::BigToLittleUINT(chunk_pack_header.alignedTo);
      chunk_pack_header.blockSize = EABig::Tools::BigToLittleUINT(chunk_pack_header.blockSize);
      chunk_pack_header.numSegments = EABig::Tools::BigToLittleUINT(chunk_pack_header.numSegments);
      chunk_pack_header.uncompressedLength = EABig::Tools::BigToLittleUINT(chunk_pack_header.uncompressedLength);
      chunk_pack_header.VersionNum = EABig::Tools::BigToLittleUINT(chunk_pack_header.VersionNum);

      for (size_t i = 0; i < chunk_pack_header.numSegments; i++)
      {
        // Get the current location.
        size_t current_location = _ftelli64(archive);
        std::cout << current_location << "\n";

        // Extract the last hexadecimal digit
        size_t lastDigit = current_location % 0x10;

        // Check if the last digit is greater than 0x08 or 0.
        if (lastDigit > 0x08 || lastDigit == 0) {
          std::cout << "The offset ends with a value greater than or equal to 0x08." << std::endl;
          current_location = EABig::Tools::roundUpToMultiple(current_location, chunk_pack_header.alignedTo);
          current_location += 8;
        }
        else {
          std::cout << "The offset does not end with a value greater than or equal to 0x08." << std::endl;
          current_location = EABig::Tools::roundUpToMultiple(current_location, 8);
        }

        _fseeki64(archive, current_location, SEEK_SET); // Seek to closest aligned location recognized by the BIG EB v3 format.

        // Read RW chunk block header.
        ChunkBlockHeader block_header = {};
        fread(&block_header, sizeof(block_header), 1, archive);
        block_header.chunkSizeCompressed = EABig::Tools::BigToLittleUINT(block_header.chunkSizeCompressed);
        block_header.compressionType = EABig::Tools::BigToLittleUINT(block_header.compressionType);

        // Allocate memory for our file buffer
        char* file_buffer = (char*)malloc(block_header.chunkSizeCompressed);
        if (file_buffer == NULL) {
          perror("Error allocating memory");
          return false;
        }

        // Read chunk into file buffer.
        fread(file_buffer, block_header.chunkSizeCompressed, 1, archive); // Read file into buffer
        std::vector<uint8_t> decompression_in_buffer_vector(file_buffer, file_buffer + block_header.chunkSizeCompressed);

        // Chunk is compressed with zlib.
        if (block_header.compressionType == 1) {
          // Allocate a buffer for the decompressed data
          size_t chunk_decompressed_size = chunk_pack_header.uncompressedLength;
          unsigned char* decompressed_data = new unsigned char[chunk_decompressed_size];

          // Decompress the data
          if (decompress_deflate(decompression_in_buffer_vector.data(),
            block_header.chunkSizeCompressed, decompressed_data, chunk_decompressed_size)) {
            decompressed_size += chunk_decompressed_size;
            decompression_out_buffer_vector.insert(decompression_out_buffer_vector.end(), decompressed_data, decompressed_data + chunk_decompressed_size);
            // Delete buffers.
            delete[] decompressed_data;
          }
          else { // Failed decompression.
            MessageBoxA(0, "Decompression failed! Reach out to GHFear for support.", "Unpacker Prompt", MB_OK | MB_ICONINFORMATION);
            delete[] decompressed_data;
            free(file_buffer);
            return false;
          }
        }

        // Chunk is compressed with refpack.
        else if (block_header.compressionType == 2) {
          std::vector<uint8_t> chunk_decompression_out_buffer_vector = refpack::decompress(decompression_in_buffer_vector);
          decompressed_size += chunk_decompression_out_buffer_vector.size();
          decompression_out_buffer_vector.insert(decompression_out_buffer_vector.end(), chunk_decompression_out_buffer_vector.begin(), chunk_decompression_out_buffer_vector.end());
        }

        // Chunk is compressed with lzx.
        else if (block_header.compressionType == 3) {

          // Check uncompress type and set length / size. (single or multi chunk)
          int uncompressed_size = chunk_pack_header.uncompressedLength;
          if (chunk_pack_header.numSegments > 1 && i != (chunk_pack_header.numSegments - 1)) {
            uncompressed_size = chunk_pack_header.blockSize;
          }
          else if (chunk_pack_header.numSegments > 1 && i == (chunk_pack_header.numSegments - 1)) {
            uncompressed_size = chunk_pack_header.uncompressedLength - decompressed_size;
          }

          // Create decompressed data buffer.
          unsigned char* decompressed_data = new unsigned char[uncompressed_size];

          //Decompress LZX data buffer.
          if (appDecompressLZX(decompression_in_buffer_vector.data(), block_header.chunkSizeCompressed, decompressed_data, uncompressed_size)) {
            decompressed_size += uncompressed_size;
            decompression_out_buffer_vector.insert(decompression_out_buffer_vector.end(), decompressed_data, decompressed_data + uncompressed_size);
            // Delete buffers.
            delete decompressed_data;
          }
          else { // Failed decompression.
            MessageBoxA(0, "Decompression failed! Reach out to GHFear for support.", "Unpacker Prompt", MB_OK | MB_ICONINFORMATION);
            delete[] decompressed_data;
            free(file_buffer);
            return false;
          }
        }

        // Chunk is not compressed
        else if (block_header.compressionType == 4) {
          decompression_out_buffer_vector.insert(decompression_out_buffer_vector.end(), decompression_in_buffer_vector.begin(), decompression_in_buffer_vector.end());
        }
        else {
          MessageBoxA(0, "Unknown compression type! Reach out to GHFear for support.", "Unpacker Prompt", MB_OK | MB_ICONINFORMATION);
          free(file_buffer);
          return false;
        }

        free(file_buffer);
      }

      // Attempt to create the directory
      if (EABig::Tools::CreateDirectoryRecursively(Filedirectory.c_str())) {
        wprintf(L"Directory created: %s\n", Filedirectory.c_str());
      }
      else {
        wprintf(L"Failed to create directory or directory already exists: %s\n", Filedirectory.c_str());
      }

      // Write to file.
      FILE* file = nullptr;
      if (!IoTools::write_file(file, Filepath, (char*)decompression_out_buffer_vector.data(), chunk_pack_header.uncompressedLength)) {
        return false;
      }

      return true;
    }

    bool unpack_uncompressed_file(FILE* archive, DWORD SIZE, std::wstring Filedirectory, std::wstring Filepath)
    {
      // Allocate memory for our file buffer
      char* file_buffer = (char*)malloc(SIZE);
      if (file_buffer == NULL) {
        perror("Error allocating memory");
        return false;
      }

      fread(file_buffer, SIZE, 1, archive); // Read file into buffer

      // Attempt to create the directory
      if (EABig::Tools::CreateDirectoryRecursively(Filedirectory.c_str())) {
        wprintf(L"Directory created: %s\n", Filedirectory.c_str());
      }
      else {
        wprintf(L"Failed to create directory or directory already exists: %s\n", Filedirectory.c_str());
      }

      // Write to file.
      FILE* file = nullptr;
      if (!IoTools::write_file(file, Filepath, file_buffer, SIZE)) {
        free(file_buffer);
        return false;
      }

      free(file_buffer);
      return true;
    }

    void failed_to_unpack_messagebox(std::string input_msg)
    {
      std::string output_string = " Failed out unpack.";
      output_string += input_msg;
      MessageBoxA(0, output_string.c_str(), "Unpacker Prompt", MB_OK | MB_ICONINFORMATION);
    }

    bool unpack_refpack_file(FILE* archive, DWORD SIZE, std::wstring Filedirectory, std::wstring Filepath)
    {
      // Allocate memory for our file buffer
      char* file_buffer = (char*)malloc(SIZE);
      if (file_buffer == NULL) {
        perror("Error allocating memory");
        return false;
      }

      // Read file into buffer
      fread(file_buffer, SIZE, 1, archive);

      // Decompress
      std::vector<uint8_t> decompression_in_buffer_vector(file_buffer, file_buffer + SIZE);
      std::vector<uint8_t> decompression_out_buffer_vector = refpack::decompress(decompression_in_buffer_vector);
      size_t decompressed_size = decompression_out_buffer_vector.size();

      // Attempt to create the directory
      if (EABig::Tools::CreateDirectoryRecursively(Filedirectory.c_str())) {
        wprintf(L"Directory created: %s\n", Filedirectory.c_str());
      }
      else {
        wprintf(L"Failed to create directory or directory already exists: %s\n", Filedirectory.c_str());
      }

      // Write to file.
      FILE* file = nullptr;
      if (!IoTools::write_file(file, Filepath, (char*)decompression_out_buffer_vector.data(), decompressed_size)) {
        free(file_buffer);
        return false;
      }

      free(file_buffer);
      return true;
    }

    // Function to parse files from a big eb archive. (Doesn't decompress yet)
    ParserResult ParseBigEB(const wchar_t* InPath, bool bUnpack, int64_t SelectedIndexToUnpack)
    {
      // Setup return type
      ParserResult RESULT = {};

      // Declare local variables.
      FILE* archive = nullptr;
      BigEBArchiveHeader archive_header = {};
      std::vector<UINT64> toc_offset_Vector;
      std::vector<UINT64> offset_Vector;
      std::vector<DWORD> size_Vector;
      std::vector<BYTE> compression_type_Vector;

      DWORD folders_offset = 0;

      _wfopen_s(&archive, InPath, L"rb");
      if (archive == NULL) {
        perror("Error opening archive");
        return RESULT;
      }

      // Save start position.
      uint64_t start_of_archive = _ftelli64(archive);

      // Save archive size and go back to start position.
      fseek(archive, 0, SEEK_END);
      uint64_t archive_size = _ftelli64(archive);
      fseek(archive, start_of_archive, SEEK_SET);

      // Read archive header and convert to little endian.
      fread(&archive_header, sizeof(archive_header), 1, archive);
      archive_header.Signature = EABig::Tools::BigToLittleUINT(archive_header.Signature);
      archive_header.HeaderVersion = EABig::Tools::BigToLittleUINT(archive_header.HeaderVersion);
      archive_header.FileAmount = EABig::Tools::BigToLittleUINT(archive_header.FileAmount);
      archive_header.Flags = EABig::Tools::BigToLittleUINT(archive_header.Flags);
      archive_header.NamesOffset = EABig::Tools::BigToLittleUINT(archive_header.NamesOffset);
      archive_header.NamesSize = EABig::Tools::BigToLittleUINT(archive_header.NamesSize);
      archive_header.FolderAmount = EABig::Tools::BigToLittleUINT(archive_header.FolderAmount);
      archive_header.ArchiveSize = EABig::Tools::BigToLittleUINT(archive_header.ArchiveSize);
      archive_header.FatSize = EABig::Tools::BigToLittleUINT(archive_header.FatSize);

      if (archive_header.FileAmount > archive_size ||
        archive_header.ArchiveSize > archive_size || archive_header.FatSize > archive_size) {
        fclose(archive);
        return RESULT;
      }

      // Set folder offset.
      folders_offset = archive_header.FileAmount;
      folders_offset *= archive_header.FilenameLength;
      folders_offset += archive_header.NamesOffset;
      folders_offset = EABig::Tools::roundUpToMultiple(folders_offset, 0x10);
      archive_header.FilenameLength -= 2;

      _fseeki64(archive, 0x30, SEEK_SET);
      for (size_t i = 0; i < archive_header.FileAmount; i++)
      {
        // Build toc_index (or whatever we would call this)
        TOCIndex toc_index = {};
        toc_offset_Vector.push_back(_ftelli64(archive));
        fread(&toc_index, sizeof(toc_index), 1, archive);
        toc_index.offset = EABig::Tools::BigToLittleUINT(toc_index.offset);
        toc_index.compressed_size = EABig::Tools::BigToLittleUINT(toc_index.compressed_size);
        toc_index.size = EABig::Tools::BigToLittleUINT(toc_index.size);
        toc_index.hash1 = EABig::Tools::BigToLittleUINT(toc_index.hash1);
        toc_index.hash2 = EABig::Tools::BigToLittleUINT(toc_index.hash2);

        if (toc_index.offset > archive_size ||
          toc_index.compressed_size > archive_size || toc_index.size > archive_size) {
          fclose(archive);
          return RESULT;
        }

        if (!(archive_header.Flags & FLAG_64BITHASH)) {
          _fseeki64(archive, -4, SEEK_CUR);
        }

        // Push values into vector.
        UINT64 OFFSET64 = toc_index.offset;
        OFFSET64 = OFFSET64 << archive_header.Alignment;
        offset_Vector.push_back(OFFSET64);
        size_Vector.push_back(toc_index.size);
      }

      for (size_t i = 0; i < archive_header.FileAmount; i++)
      {
        // Read compression type. (This doesn't exist on all versions of EB V3 and you may need to figure this out some other way.)
        BYTE compression_type = 0;
        fread(&compression_type, sizeof(compression_type), 1, archive);
        compression_type_Vector.push_back(compression_type);
      }

      _fseeki64(archive, archive_header.NamesOffset, SEEK_SET);
      for (size_t i = 0; i < archive_header.FileAmount; i++)
      {
        // Declare local variables.
        ParserIndexResult ParserIndex = {};

        std::wstring wide_archiv_path = InPath;
        std::wstring directory = EABig::Tools::Strings::ParseFilePath(wide_archiv_path).first;
        std::wstring out_filepath = directory;
        std::wstring out_filedirectory = directory;

        // Read the folder number and convert to little endian.
        WORD folder_number = 0;
        fread(&folder_number, sizeof(folder_number), 1, archive);
        folder_number = EABig::Tools::BigToLittleUINT(folder_number);

        // Read and build directory. (Stage 1: Filename) 
        std::vector<char> name_buffer(archive_header.FilenameLength);
        fread(name_buffer.data(), archive_header.FilenameLength, 1, archive);
        std::string name(name_buffer.begin(), std::find(name_buffer.begin(), name_buffer.end(), '\0'));
        std::string filename = name;

        DWORD next_name_offset = _ftelli64(archive); // Save the current location as next name offset location.
        DWORD foldername_offset = folder_number;
        foldername_offset *= archive_header.FoldernameLength;
        foldername_offset += folders_offset;

        _fseeki64(archive, foldername_offset, SEEK_SET); // Seek to foldername location in archive.

        // Read and build directory. (Stage 2: directory)
        std::vector<char> folder_buffer(archive_header.FoldernameLength);
        fread(folder_buffer.data(), archive_header.FoldernameLength, 1, archive);
        std::string folder(folder_buffer.begin(), std::find(folder_buffer.begin(), folder_buffer.end(), '\0'));
        std::string final_extracted_filepath = folder;
        final_extracted_filepath += "/";
        final_extracted_filepath += name;

        _fseeki64(archive, offset_Vector[i], SEEK_SET); // Seek to file offset location.

        // Prepare final wide character strings. (Stage 3: Build proper path)
        out_filedirectory += EABig::Tools::Strings::to_wstring(folder);
        std::replace(out_filepath.begin(), out_filepath.end(), L'/', L'\\');
        std::replace(out_filedirectory.begin(), out_filedirectory.end(), L'/', L'\\');
        out_filepath += EABig::Tools::Strings::to_wstring(final_extracted_filepath);

        std::filesystem::path path(folder + "/" + name);

        if (!bUnpack) {
          // Set Parsed_Archive struct members.
          ParserIndex.Offset = offset_Vector[i];
          ParserIndex.Size = size_Vector[i];
          ParserIndex.FileDirectory = path.parent_path().string();
          ParserIndex.FileName = path.stem().string();
          ParserIndex.FileExtension = path.extension().string();
        }

        // Read RW chunk pack header.
        ChunkPackHeader chunk_pack_header = {};
        fread(&chunk_pack_header, sizeof(chunk_pack_header), 1, archive);
        std::string chunkpack_id = chunk_pack_header.ID;

        _fseeki64(archive, offset_Vector[i], SEEK_SET); // Seek to file offset location.

        // Parse and unpack conditions.
        bool full_archive_unpack = bUnpack && SelectedIndexToUnpack == -1; // Do a full archive unpack.
        bool single_archive_unpack = bUnpack && SelectedIndexToUnpack == i; // Do a single file archive unpack.

        // Run the unpacking functions and set the file types.
        // Chunk packed with Refpack compression.
        if (chunkpack_id == "chunkref" && size_Vector[i] > 0) {
          ParserIndex.CompressionType = "chunkref";
          if (full_archive_unpack || single_archive_unpack) {
            if (!chunk_decompress(archive, out_filedirectory, out_filepath)) {
              fclose(archive);
              return RESULT;
            }
          }
        }
        // Chunk packed with Zlib/Deflate compression.
        else if (chunkpack_id == "chunkzip" && size_Vector[i] > 0) {
          ParserIndex.CompressionType = "chunkzip";
          if (full_archive_unpack || single_archive_unpack) {
            if (!chunk_decompress(archive, out_filedirectory, out_filepath)) {
              fclose(archive);
              return RESULT;
            }
          }
        }
        // Chunk packed with LZX compression.
        else if (chunkpack_id == "chunklzx" && size_Vector[i] > 0) {
          ParserIndex.CompressionType = "chunklzx";
          if (full_archive_unpack || single_archive_unpack) {
            if (!chunk_decompress(archive, out_filedirectory, out_filepath)) {
              fclose(archive);
              return RESULT;
            }
          }
        }
        // If we enter here, the file is not chunk packed.
        else if (size_Vector[i] > 0) {
          WORD refpack_magic = 0;
          fread(&refpack_magic, sizeof(WORD), 1, archive);
          refpack_magic = EABig::Tools::BigToLittleUINT(refpack_magic);
          _fseeki64(archive, offset_Vector[i], SEEK_SET); // Seek to file offset location.

          // Parse/Unpack single file compressed with refpack
          if (refpack_magic == 0x10FB) {
            ParserIndex.CompressionType = "Refpack";
            if (full_archive_unpack || single_archive_unpack) {
              if (!unpack_refpack_file(archive, size_Vector[i], out_filedirectory, out_filepath)) {
                fclose(archive);
                return RESULT;
              }
            }
          }
          else { // Parse/Unpack single file without compression.
            ParserIndex.CompressionType = "None";
            if (full_archive_unpack || single_archive_unpack) {
              if (!unpack_uncompressed_file(archive, size_Vector[i], out_filedirectory, out_filepath)) {
                fclose(archive);
                return RESULT;
              }
            }
          }
        }
        // Parse/Unpack empty file. (any type)
        else if (size_Vector[i] == 0) {
          ParserIndex.CompressionType = "Empty";
          if (full_archive_unpack || single_archive_unpack) {
            if (!unpack_uncompressed_file(archive, size_Vector[i], out_filedirectory, out_filepath)) {
              fclose(archive);
              return RESULT;
            }
          }
        }

        // Clear buffers.
        name_buffer.clear();
        name.clear();
        folder_buffer.clear();
        folder.clear();

        // Push to archive UI struct.
        RESULT.ParserData.push_back(ParserIndex);
        _fseeki64(archive, next_name_offset, SEEK_SET); // Seek to the saved next name location.
      }

      // Clear buffers.
      toc_offset_Vector.clear();
      offset_Vector.clear();
      size_Vector.clear();
      compression_type_Vector.clear();

      // Close the archive.
      fclose(archive);
      RESULT.bSuccess = true;
      return RESULT;
    }

  } // End of BigEB namespace.

} // End of EASportsToolBox namespace.


