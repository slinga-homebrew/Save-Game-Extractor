#
# Save Game Extractor (GPL3)
# https://github.com/slinga-homebrew/Save-Game-Extractor
#
# This script parses an encoded transmission from the Sega Saturn,
# validates it, and writes it out to disk
#
# The transmission consists of a TRANSMISSION_HEADER followed by a
# variable number of bytes of data. The transmission is zipped,
# Reed Solomon encoded, and then escaped. This Python script
# undoes all of that.
#

import sys
import binascii
import hashlib
import reedsolo
import zlib

'''
Taken from main.h
typedef struct _TRANSMISSION_HEADER
{
    char magic[TRANSMISSION_MAGIC_SIZE]; // magic bytes be SGEX
    unsigned char md5Hash[MD5_HASH_SIZE];
    char saveFilename[MAX_SAVE_FILENAME]; // save filename
    unsigned int saveFileSize;  // size of the file in bytes
    unsigned char saveFileData[0]; // saveFileSize number of bytes of save data
} TRANSMISSION_HEADER, *PTRANSMISSION_HEADER;
'''

MAGIC = "SGEX"
TRANSMISSION_HEADER_SIZE = 36

ESCAPE_BYTE = 0x54
SYNC_REPLACE = 0x9F
SYNC_BYTE = 0xAB

# Change two ESCAPE_BYTEs in a row to a single ESCAPE_BYTE
# Change an ESCAPE_BYTE followed by SYNC_REPLACE byte to a single SYNC_BYTE
def unescape(message):

    escapedMessage = ""

    i = 0

    while(i < len(message)):

        if message[i] == chr(ESCAPE_BYTE):

            if message[i + 1] == chr(ESCAPE_BYTE):

                # two ESCAPE_BYTES, replace with a single ESCAPE_BYTE
                escapedMessage = escapedMessage + chr(ESCAPE_BYTE)
                i = i + 1

            elif message[i + 1] == chr(SYNC_REPLACE):

                # ESCAPE_BYTE followed by a SYNC_REPLACE
                # replace with a SYNC_BYTE
                escapedMessage = escapedMessage + chr(SYNC_BYTE)
                i = i + 1

            else:

                # invalid escape sequence data, the data is corrupted
                return ""
        else:

            # not an escape byte, continue as normal
            escapedMessage = escapedMessage + message[i]

        i += 1

    return escapedMessage

def main():

    print("Save Game Extractor");
    print("(github.com/slinga-homebrew/Save-Game-Extractor)\n")

    if len(sys.argv) != 2:
        print("Error: Input filename required")
        return -1


    filename = sys.argv[1]

    try:
        inFile = open(filename, "rb")
    except:
        print("Error: Could not open " + filename + " for reading")
        return -1

    #
    # Unescape the buffer
    #

    escapedBuf = inFile.read()

    # unescape the buffer
    unescapedBuf = unescape(escapedBuf)
    if unescapedBuf == "":
        print("Failed to unescape data, something is corrupt.");
        return -1

    #
    # Reed Solomon decode
    #

    # Reed Solomon parameters must match settings used by libcorrect
    rsc = reedsolo.RSCodec(nsym=32, nsize=255, fcr=1, prim=0x187)

    try:
        decodedBuf = rsc.decode(unescapedBuf)
    except:
        print("Reed Solomon couldn't decode buffer, too many errors.")
        print(sys.exc_info()[0])
        return -1

    print("Errors Corrected: " + str(len(decodedBuf[2])))

    compressedBuf = decodedBuf[0]

    #
    # Decompress the data
    #
    decompressedBuf = zlib.decompress(str(compressedBuf));

    #
    # TRANSMISSION_HEADER + variable length save data
    #

    # sanity check the buffer
    if len(decompressedBuf) < TRANSMISSION_HEADER_SIZE:
        print("Error: " + filename + " is too small. Must be at least TRANSMISSION_HEADER_SIZE")
        return -1

    # SGEX magic bytes
    magic = decompressedBuf[0:4].decode("utf-8")
    if magic != MAGIC:
        print magic
        print("Error: The magic bytes are invalid")
        return -1

    saveSize = binascii.b2a_hex(decompressedBuf[32:36])
    saveSize = int(saveSize, 16)

    # validate length, shouldn't fail here because of the Reed Solomon check
    if TRANSMISSION_HEADER_SIZE + saveSize != len(decompressedBuf):
        print("Error: Received incorrect number of bytes. Expected " + str(TRANSMISSION_HEADER_SIZE + saveSize) + ", got " + str(len(decompressedBuf)))
        return -1

    saveName = decompressedBuf[20:31].decode("utf-8")
    md5Hash = binascii.b2a_hex(decompressedBuf[4:20]).decode("utf-8")

    # verify the MD5 hash. Again shouldn't ever fail here due to the Reed Solomon check
    computedHashResult = hashlib.md5(decompressedBuf[TRANSMISSION_HEADER_SIZE:])
    computedHash = computedHashResult.hexdigest()

    print("Transmitted Filename: " + saveName)
    print("Transmitted Save Size: " + str(saveSize))
    print("Transmitted MD5: " + str(md5Hash))
    print("Computed MD5: " + computedHash)
    print("")

    if md5Hash != computedHash:
        print("MD5 hashes don't match, save is corrupt.")
    else:
        print("MD5 hashes validate, save is correct.")

    # create the output file
    try:
        outFile = open(saveName, "wb")
        outFile.write(decompressedBuf[36:])
        outFile.close()
    except:
        print("Error writing save " + saveName + " to disk")
        return -1

    print("Wrote save game " + saveName + " to disk")

if __name__ == "__main__":
    main()
