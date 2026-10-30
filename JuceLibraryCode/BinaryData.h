/* =========================================================================================

   This is an auto-generated file: Any edits you make may be overwritten!

*/

#pragma once

namespace BinaryData
{
    extern const char*   Anleitung_pdf;
    const int            Anleitung_pdfSize = 1420609;

    extern const char*   dawnotes_png;
    const int            dawnotes_pngSize = 145999;

    extern const char*   dawnnotes_ico;
    const int            dawnnotes_icoSize = 4286;

    extern const char*   header_banner_gif;
    const int            header_banner_gifSize = 127257;

    extern const char*   set_german_ini;
    const int            set_german_iniSize = 18729;

    // Number of elements in the namedResourceList and originalFileNames arrays.
    const int namedResourceListSize = 5;

    // Points to the start of a list of resource names.
    extern const char* namedResourceList[];

    // Points to the start of a list of resource filenames.
    extern const char* originalFilenames[];

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding data and its size (or a null pointer if the name isn't found).
    const char* getNamedResource (const char* resourceNameUTF8, int& dataSizeInBytes);

    // If you provide the name of one of the binary resource variables above, this function will
    // return the corresponding original, non-mangled filename (or a null pointer if the name isn't found).
    const char* getNamedResourceOriginalFilename (const char* resourceNameUTF8);
}
