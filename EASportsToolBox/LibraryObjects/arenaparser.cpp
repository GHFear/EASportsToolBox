// EA Skate Arena Parser By GHFear.
#pragma once
#include <refpack/refpackd.h>
#include "tools.h"
#include "EASportsToolBox.h"

namespace EASportsToolBox 
{
  namespace Arena
  {
    // Function to unpack files from an Arena file package.
    ParserResult ParseArena(std::wstring InPath, bool bUnpack, int64_t SelectedIndexToUnpack) {
      // Setup return type
      ParserResult RESULT = {};

      // Declare local variables.
      FILE* archive = nullptr;
      FILE* file = nullptr;
      std::wstring out_directory = EABig::Tools::Strings::ParseFilePath(InPath).first
        + EABig::Tools::Strings::GetFilenameWithoutExtension(InPath.c_str());

      _wfopen_s(&archive, InPath.c_str(), L"rb");
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

      _fseeki64(archive, 32, SEEK_SET); // Seek to file_count.

      // Read filecount and convert to little endian.
      uint32_t file_count = 0;
      fread(&file_count, sizeof(uint32_t), 1, archive);
      file_count = EABig::Tools::BigToLittleUINT(file_count);

      _fseeki64(archive, 48, SEEK_SET); // Seek to file_table_offset

      // Read file table offset and convert to little endian.
      uint32_t file_table_offset = 0;
      fread(&file_table_offset, sizeof(uint32_t), 1, archive);
      file_table_offset = EABig::Tools::BigToLittleUINT(file_table_offset);

      if (file_table_offset > archive_size)
      {
        fclose(archive);
        return RESULT;
      }

      // Loop through all Arena objects.
      uint32_t object_index = 0;
      for (size_t i = 0; i < file_count; i++)
      {
        ParserIndexResult ParserIndex = {};

        std::wstring out_path = out_directory + L"\\";
        bool name_found = false;

        _fseeki64(archive, file_table_offset + (i * 24), SEEK_SET); // Seek to file toc.

        // Read TOC data and convert to little endian.
        uint32_t offset = 0;
        fread(&offset, sizeof(uint32_t), 1, archive);
        offset = EABig::Tools::BigToLittleUINT(offset);

        uint64_t size = 0;
        fread(&size, sizeof(uint64_t), 1, archive);
        size = EABig::Tools::BigToLittleUINT(size);

        uint64_t unknown1 = 0;
        fread(&unknown1, sizeof(uint32_t), 1, archive);
        unknown1 = EABig::Tools::BigToLittleUINT(unknown1);

        uint64_t unknown2 = 0;
        fread(&unknown2, sizeof(uint32_t), 1, archive);
        unknown2 = EABig::Tools::BigToLittleUINT(unknown2);

        uint32_t file_type = 0;
        fread(&file_type, sizeof(uint32_t), 1, archive);
        file_type = EABig::Tools::BigToLittleUINT(file_type);

        if (offset > archive_size || size > archive_size)
        {
          fclose(archive);
          return RESULT;
        }

        size_t object_name_index = 0;
        for (size_t j = 0; j < sizeof(ArenaObjectTypeArray) / sizeof(ArenaObjectTypeArray[0]); j++)
        {
          if (EABig::Tools::BigToLittleUINT(ArenaObjectTypeArray[j]) == file_type)
          {
            object_name_index = j;
            name_found = true;
            break;
          }
        }

        object_index++; // Add 1 to object index.

        // Check if we found a name that matches the object ID.
        if (name_found == true)
        {
          out_path += L"Object_" + std::to_wstring(object_index) + L"_"
            + EABig::Tools::Strings::to_wstring(ArenaObjectTypeNames[object_name_index]);
          ParserIndex.FileName = ArenaObjectTypeNames[object_name_index];
        }
        else
        {
          out_path += L"Object_" + std::to_wstring(object_index) + L"_" + L"UNKNOWN_TYPE";
          ParserIndex.FileName = "UNKNOWN_TYPE";
        }

        // If offset is 0, handle differently and assume the offset is the start of the first Arena Object.
        if (offset == 0)
        {
          _fseeki64(archive, 68, SEEK_SET);
          uint32_t local_size = 0;
          fread(&local_size, sizeof(uint32_t), 1, archive);
          local_size = EABig::Tools::BigToLittleUINT(local_size);
          offset = offset + local_size;
        }

        // Build parse struct.
        ParserIndex.Offset = offset;
        ParserIndex.Size = size;
        ParserIndex.Offset = offset;

        _fseeki64(archive, offset, SEEK_SET); // Seek to file offset.

        bool full_archive_unpack = bUnpack && SelectedIndexToUnpack == -1; // Do a full archive unpack.
        bool single_archive_unpack = bUnpack && SelectedIndexToUnpack == i; // Do a single file archive unpack.

        if (full_archive_unpack || single_archive_unpack)
        {
          // Attempt to create the directory
          if (EABig::Tools::CreateDirectoryRecursively(out_directory.c_str())) {
            wprintf(L"Directory created: %s\n", out_directory.c_str());
          }
          else {
            wprintf(L"Failed to create directory or directory already exists: %s\n", out_directory.c_str());
          }

          char* out_buffer = (char*)malloc(size);
          if (out_buffer == nullptr)
          {
            fclose(archive);
            MessageBox(0, L"Error creating Arena out_buffer!  \nUnpacker is unable to continue!",
              L"Unpacker Prompt", MB_OK | MB_ICONINFORMATION);
            return RESULT;
          }

          fread(out_buffer, size, 1, archive);

          // Write to file.
          if (!IoTools::write_file(file, out_path, out_buffer, size))
          {
            fclose(archive);
            free(out_buffer);
            MessageBox(
              0, L"Error writing data! \nMake sure you don't have a handle to a file from some other tool! \nUnpacker is unable to continue!",
              L"Unpacker Prompt", MB_OK | MB_ICONINFORMATION);
            return RESULT;
          }

          free(out_buffer);
        }

        // Push information to vector.
        RESULT.ParserData.push_back(ParserIndex);
      }

      fclose(archive);
      RESULT.bSuccess = true;
      return RESULT;
    }

  } // End of Arena namespace.

} // End of EASportsToolBox namespace.

