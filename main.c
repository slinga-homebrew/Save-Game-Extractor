// Save Game Extractor - a utility that transmits Sega Saturn save game files over audio. Based on minimodem.

/*
** Jo Sega Saturn Engine
** Copyright (c) 2012-2017, Johannes Fetz (johannesfetz@gmail.com)
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of the Johannes Fetz nor the
**       names of its contributors may be used to endorse or promote products
**       derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
** WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
** DISCLAIMED. IN NO EVENT SHALL Johannes Fetz BE LIABLE FOR ANY
** DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
** (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
** LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
** ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
** SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <jo/jo.h>
#include "main.h"
#include "util.h"
#include "encode.h"
#include "md5/md5.h"
#include "saturn-minimodem.h"

GAME g_Game = {0};
SAVES g_Saves[MAX_SAVES] = {0};

void jo_main(void)
{
    int result = 0;

    jo_core_init(JO_COLOR_Black);

    // increase the default heap size. LWRAM is not being used
    jo_add_memory_zone((unsigned char *)LWRAM, LWRAM_HEAP_SIZE);

    // allocate our save file buffer
    // the buffer consists of the transmission header + bup header + save data
    g_Game.transmissionData = jo_malloc(TRANSMISSION_HEADER_SIZE + BUP_HEADER_SIZE + MAX_SAVE_SIZE);
    if(g_Game.transmissionData == NULL)
    {
        jo_core_error("Failed to allocated save file data buffer!!");
        return;
    }
    g_Game.saveFileData = g_Game.transmissionData + TRANSMISSION_HEADER_SIZE + BUP_HEADER_SIZE;

    // init Saturn minimodem
    result = SaturnMinimodem_init();
    if(result != 0)
    {
        jo_core_error("Failed to initialize minimodem!!");
        return;
    }

    // init Reed Solomon encoder
    g_reedSolomon = correct_reed_solomon_create(correct_rs_primitive_polynomial_ccsds,
                                                RS_FIRST_CONSECUTIVE_ROOT,
                                                RS_ROOT_GAP,
                                                RS_NUM_ROOTS);
    if(g_reedSolomon == NULL)
    {
        jo_core_error("Failed to init Reed Solomon");
        return;
    }

    // ABC + start handler
    jo_core_set_restart_game_callback(abcStartHandler);

    // callbacks
    jo_core_add_callback(main_draw);
    jo_core_add_callback(main_input);

    jo_core_add_callback(listSaves_draw);
    jo_core_add_callback(listSaves_input);

    jo_core_add_callback(playSaves_draw);
    jo_core_add_callback(playSaves_input);

    jo_core_add_callback(dumpBios_draw);
    jo_core_add_callback(dumpBios_input);

    jo_core_add_callback(test_draw);
    jo_core_add_callback(test_input);

    jo_core_add_callback(collect_draw);
    jo_core_add_callback(collect_input);

    jo_core_add_callback(credits_draw);
    jo_core_add_callback(credits_input);

    // debug output
    //jo_core_add_callback(debugOutput_draw);

    // initial state
    transitionToState(STATE_MAIN);

    jo_core_run();
}

// useful debug output
void debugOutput_draw()
{
    jo_printf(2, 0, "Memory Usage: %d Frag: %d" , jo_memory_usage_percent(), jo_memory_fragmentation());
    jo_printf(2, 1, "Save: %d CO: %d" , g_Game.numSaves, g_Game.cursorOffset);

    /*
    if(jo_memory_fragmentation() > 100)
    {
        jo_core_error("High fragmentation!! %d", jo_memory_fragmentation());
        jo_reduce_memory_fragmentation();
        jo_core_error("High fragmentation!! %d", jo_memory_fragmentation());
    }
    */
}

// restarts the program if controller one presses ABC+Start
// this definitely leaks memory but whatever...
// maybe I should just call jo_main()?
void abcStartHandler(void)
{
    g_Game.input.pressedStartAC = true;
    g_Game.input.pressedB = true;
    transitionToState(STATE_MAIN);
    return;
}

// helper for transitioning between program states
// adds/removes callbacks as needed
// resets globals as needed
void transitionToState(int newState)
{
    clearScreen();

    g_Game.previousState = g_Game.state;

    switch(g_Game.state)
    {
        case STATE_UNINITIALIZED:
        case STATE_MAIN:
        case STATE_LIST_SAVES:
        case STATE_PLAY_SAVES:
        case STATE_DUMP_BIOS:
        case STATE_TEST:
        case STATE_COLLECT:
        case STATE_CREDITS:
            break;

        default:
            jo_core_error("%d is an invalid current state!!", g_Game.state);
            return;
    }

    switch(newState)
    {
        case STATE_MAIN:
            g_Game.cursorPosX = CURSOR_X;
            g_Game.cursorPosY = OPTIONS_Y;
            g_Game.cursorOffset = 0;
            g_Game.numStateOptions = MAIN_NUM_OPTIONS;
            break;

        case STATE_LIST_SAVES:
            g_Game.cursorPosX = CURSOR_X;
            g_Game.cursorPosY = OPTIONS_Y + 1;
            g_Game.cursorOffset = 0;
            g_Game.numStateOptions = 0; // 0 options until we list the number of saves
            g_Game.numSaves = 0; // number of saves counted
            g_Game.listedSaves = false;
            break;

        case STATE_DUMP_BIOS:
            g_Game.cursorPosX = CURSOR_X;
            g_Game.cursorPosY = OPTIONS_Y + 1;
            g_Game.cursorOffset = 0;
            g_Game.numStateOptions = BIOS_NUM_OPTIONS;
            break;

        case STATE_PLAY_SAVES:
            g_Game.md5Calculated = false;
            g_Game.isTransmissionRunning = false;

            if(g_Game.encodedTransmissionData != NULL)
            {
                jo_free(g_Game.encodedTransmissionData);
                g_Game.encodedTransmissionData = NULL;
            }
            g_Game.encodedTransmissionSize = 0;
            break;

        case STATE_TEST:
            g_Game.isTransmissionRunning = false;
            break;

        case STATE_COLLECT:
            break;

        case STATE_CREDITS:
            break;

        default:
            jo_core_error("%d is an invalid state!!", newState);
            return;
    }

    g_Game.state = newState;

    return;
}

// draws the main option screen
// Number of options are hardcoded
void main_draw(void)
{
    unsigned int y = 0;

    if(g_Game.state != STATE_MAIN)
    {
        return;
    }

    // heading
    jo_printf(HEADING_X, HEADING_Y + y++, "Save Game Extractor Ver %s", VERSION);
    jo_printf(HEADING_X, HEADING_Y + y++, HEADING_UNDERSCORE);

    y = 0;

    // options
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Internal Memory");
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Cartridge Memory");
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "External Device (Floppy)");
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Dump Bios");
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Test Audio Transmission");
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Save Games Collect Project");
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Credits");

    // cursor
    jo_printf(g_Game.cursorPosX, g_Game.cursorPosY + g_Game.cursorOffset, ">>");

    return;
}

// checks for up/down movement for the cursor
// erases the old one and draws a new one
// savesPage is set to true if the cursor is for the list saves page
void moveCursor(bool savesPage)
{
    int cursorOffset = g_Game.cursorOffset;
    int maxCursorOffset = g_Game.numStateOptions;

    // do nothing if there are no options
    // this can happen if we select a backup device with no saves for example
    if(g_Game.numStateOptions == 0)
    {
        return;
    }

    if (jo_is_pad1_key_pressed(JO_KEY_UP))
    {
        if(g_Game.input.pressedUp == false)
        {
            cursorOffset--;
        }
        g_Game.input.pressedUp = true;
    }
    else
    {
        g_Game.input.pressedUp = false;
    }

    if (jo_is_pad1_key_pressed(JO_KEY_DOWN))
    {
        if(g_Game.input.pressedDown == false)
        {
            cursorOffset++;
        }
        g_Game.input.pressedDown = true;
    }
    else
    {
        g_Game.input.pressedDown = false;
    }

    // if we moved the cursor, erase the old
    if(cursorOffset != g_Game.cursorOffset)
    {
        if(savesPage == true)
        {
            jo_printf(g_Game.cursorPosX, g_Game.cursorPosY + (g_Game.cursorOffset % MAX_SAVES_PER_PAGE), "  ");
        }
        else
        {
            jo_printf(g_Game.cursorPosX, g_Game.cursorPosY + g_Game.cursorOffset, "  ");
        }
    }

    // validate the number of lives
    if(cursorOffset < 0)
    {
        cursorOffset = maxCursorOffset - 1;
    }

    if(cursorOffset >= maxCursorOffset)
    {
        cursorOffset = 0;
    }
    g_Game.cursorOffset = cursorOffset;
}

// handles input on the main screen
void main_input(void)
{
    if(g_Game.state != STATE_MAIN)
    {
        return;
    }

    // did the player hit start
    if(jo_is_pad1_key_pressed(JO_KEY_START) ||
       jo_is_pad1_key_pressed(JO_KEY_A) ||
       jo_is_pad1_key_pressed(JO_KEY_C))
    {
        if(g_Game.input.pressedStartAC == false)
        {
            g_Game.input.pressedStartAC = true;

            switch(g_Game.cursorOffset)
            {

                case MAIN_OPTION_INTERNAL:
                {
                    g_Game.backupDevice = JoInternalMemoryBackup;
                    transitionToState(STATE_LIST_SAVES);
                    return;
                }
                case MAIN_OPTION_CARTRIDGE:
                {
                    g_Game.backupDevice = JoCartridgeMemoryBackup;
                    transitionToState(STATE_LIST_SAVES);
                    return;
                }
                case MAIN_OPTION_EXTERNAL:
                {
                    g_Game.backupDevice = JoExternalDeviceBackup;
                    transitionToState(STATE_LIST_SAVES);
                    return;
                }
                case MAIN_OPTION_BIOS:
                {
                    transitionToState(STATE_DUMP_BIOS);
                    return;
                }
                case MAIN_OPTION_TEST:
                {
                    transitionToState(STATE_TEST);
                    return;
                }
                case MAIN_OPTION_COLLECT:
                {
                    transitionToState(STATE_COLLECT);
                    return;
                }
                case MAIN_OPTION_CREDITS:
                {
                    transitionToState(STATE_CREDITS);
                    return;
                }
                default:
                {
                    jo_core_error("Invalid main option!!");
                    return;
                }
            }
        }
    }
    else
    {
        g_Game.input.pressedStartAC = false;
    }

    // update the cursor
    moveCursor(false);
    return;
}

// queries the saves on the backup device and fills out the fileSaves array
int readSaveFiles(jo_backup_device backupDevice, PSAVES fileSaves, unsigned int numSaves)
{
    jo_list saveFilenames = {0};
    bool result = false;
    int count = 0;

    jo_list_init(&saveFilenames);

    result = jo_backup_read_device(backupDevice, &saveFilenames);
    if(result == false)
    {
        jo_list_free_and_clear(&saveFilenames);
        return -1;
    }

    for(unsigned int i = 0; i < (unsigned int)saveFilenames.count && i < numSaves; i++)
    {
        char comment[MAX_SAVE_COMMENT] = {0};
        unsigned char language = 0;
        unsigned int date = 0;
        unsigned int numBytes = 0;
        unsigned int numBlocks = 0;

        char* filename = jo_list_at(&saveFilenames, i)->data.ch_arr;

        if(filename == NULL)
        {
            jo_core_error("readSaveFiles list is corrupt!!");
            return -1;
        }

        // query the save metadata
        result = jo_backup_get_file_info(backupDevice, filename, comment, &language, &date, &numBytes, &numBlocks);
        if(result == false)
        {
            jo_core_error("Failed to read file size!!");
            return -1;
        }

        strncpy((char*)fileSaves[i].filename, filename, MAX_SAVE_FILENAME);
        strncpy((char*)fileSaves[i].comment, comment, MAX_SAVE_COMMENT);
        fileSaves[i].language = language;
        fileSaves[i].date = date;
        fileSaves[i].datasize = numBytes;
        fileSaves[i].blocksize = numBlocks;
        count++;
    }

    jo_list_free_and_clear(&saveFilenames);

    return count;
}

// draws the list saves screen
void listSaves_draw(void)
{
    char* backupDeviceType = NULL;

    if(g_Game.state != STATE_LIST_SAVES)
    {
        return;
    }

    switch(g_Game.backupDevice)
    {
        case JoInternalMemoryBackup:
            backupDeviceType = "Internal Memory";
            break;
        case JoCartridgeMemoryBackup:
            backupDeviceType = "Cartridge Memory";
            break;
        case JoExternalDeviceBackup:
            backupDeviceType = "External Device";
            break;

        default:
            jo_core_error("Invalid backup device specified!! %d\n", g_Game.backupDevice);
            return;
    }

    jo_printf(HEADING_X, HEADING_Y, "%s", backupDeviceType);
    jo_printf(HEADING_X, HEADING_Y + 1, HEADING_UNDERSCORE);

    if(g_Game.listedSaves == false)
    {
        bool result;
        int count;

        jo_memset(g_Saves, 0, sizeof(g_Saves));

        g_Game.listedSaves = true;

        result = jo_backup_mount(g_Game.backupDevice);
        if(result == false)
        {
            jo_core_error("Failed to open internal %s!!", backupDeviceType);
            transitionToState(STATE_MAIN);
            return;
        }

        // read the saves meta data
        count = readSaveFiles(g_Game.backupDevice, g_Saves, COUNTOF(g_Saves));
        if(count > 0)
        {
            // update the count of saves
            g_Game.numSaves = count;
        }
        else
        {
            g_Game.numSaves = 0;
        }
    }

    if(g_Game.numSaves > 0)
    {
        int i = 0;
        int j = 0;

        // header
        jo_printf(OPTIONS_X, OPTIONS_Y, "%-11s  %-10s  %6s", "Filename", "Comment", "Bytes");

        // zero out the save print fields otherwise we will have stale data on the screen
        // when we go to other pages
        for(i = 0; i < MAX_SAVES_PER_PAGE; i++)
        {
            jo_printf(OPTIONS_X, OPTIONS_Y + i  + 1, "                                      ");
        }

        // print up to MAX_SAVES_PER_PAGE saves on the screen
        for(i = (g_Game.cursorOffset / MAX_SAVES_PER_PAGE) * MAX_SAVES_PER_PAGE, j = 0; i < g_Game.numSaves && j < MAX_SAVES_PER_PAGE; i++, j++)
        {
            jo_printf(OPTIONS_X, OPTIONS_Y + (i % MAX_SAVES_PER_PAGE) + 1, "%-11s  %-10s  %6d", g_Saves[i].filename, g_Saves[i].comment, g_Saves[i].datasize);
        }

        g_Game.numStateOptions = g_Game.numSaves;
    }
    else
    {
        jo_printf(OPTIONS_X, OPTIONS_Y, "Found 0 saves on the device");
    }

    if(g_Game.numStateOptions > 0)
    {
        // copy the save data
        memcpy(g_Game.saveFilename, g_Saves[g_Game.cursorOffset].filename, MAX_SAVE_FILENAME);
        strncpy(g_Game.saveComment, g_Saves[g_Game.cursorOffset].comment, MAX_SAVE_COMMENT);
        g_Game.saveLanguage = g_Saves[g_Game.cursorOffset].language;
        g_Game.saveDate = g_Saves[g_Game.cursorOffset].date;
        g_Game.saveFileSize = g_Saves[g_Game.cursorOffset].datasize;

        jo_printf(g_Game.cursorPosX, g_Game.cursorPosY + g_Game.cursorOffset % MAX_SAVES_PER_PAGE, ">>");
    }

    return;
}

// handles input on the list saves screen
// B returns to the main menu
void listSaves_input(void)
{
    if(g_Game.state != STATE_LIST_SAVES)
    {
        return;
    }

    // did the player hit start
    if(jo_is_pad1_key_pressed(JO_KEY_START) ||
       jo_is_pad1_key_pressed(JO_KEY_A) ||
       jo_is_pad1_key_pressed(JO_KEY_C))
    {
        if(g_Game.input.pressedStartAC == false)
        {
            int result = 0;

            g_Game.input.pressedStartAC = true;

            if(g_Game.numStateOptions == 0)
            {
                // if user hit A, C or start and there were no saves listed, return them back a screen
                transitionToState(STATE_MAIN);
                return;
            }

            result = copySaveFile();
            if(result != 0)
            {
                // something went wrong
                // copySaveFile calls jo_core_error() so we don't have to do anything
                transitionToState(STATE_MAIN);
            }

            transitionToState(STATE_PLAY_SAVES);
            return;
        }
    }
    else
    {
        g_Game.input.pressedStartAC = false;
    }

    if(jo_is_pad1_key_pressed(JO_KEY_B))
    {
        if(g_Game.input.pressedB == false)
        {
            g_Game.input.pressedB = true;
            transitionToState(STATE_MAIN);
            return;
        }
    }
    else
    {
        g_Game.input.pressedB = false;
    }

    // update the cursor
    moveCursor(true);
    return;
}

// draws the play saves screen
void playSaves_draw(void)
{
    int result = 0;
    int y = 0;
    unsigned int bytesTransferred = 0;
    unsigned int totalSize = 0;
    jo_backup_date jo_date = {0};

    if(g_Game.state != STATE_PLAY_SAVES)
    {
        return;
    }

    jo_printf(HEADING_X, HEADING_Y, "Transmitting Data Over Audio");
    jo_printf(HEADING_X, HEADING_Y + 1, HEADING_UNDERSCORE);

    // only compute the MD5 hash once
    if(g_Game.md5Calculated == false)
    {
        unsigned int unencodedSize = 0;
        unsigned int uncompressedSize = 0;
        unsigned char* compressedBuffer = NULL;
        g_Game.compressedSize = 0;

        //
        // print messages to the user so that can get an estimate of the time for longer operations
        //
        result = calculateMD5Hash(g_Game.saveFileData, g_Game.saveFileSize, g_Game.md5Hash);
        if(result != 0)
        {
            // something went wrong
            transitionToState(STATE_MAIN);
            return;
        }

        // transmission header
        result = initializeTransmissionHeader(g_Game.md5Hash, sizeof(g_Game.md5Hash), g_Game.saveFilename, g_Game.saveFileSize);
        if(result != 0)
        {
            // something went wrong
            transitionToState(STATE_MAIN);
            return;
        }

        // bup header
        initializeBUPHeader(g_Game.saveFilename, g_Game.saveComment, g_Game.saveLanguage, g_Game.saveDate, g_Game.saveFileSize);
        if(result != 0)
        {
            // something went wrong
            transitionToState(STATE_MAIN);
            return;
        }

        //
        // Compress the save
        //

        // estimate the compressed output size
        uncompressedSize = TRANSMISSION_HEADER_SIZE + BUP_HEADER_SIZE + g_Game.saveFileSize;
        g_Game.compressedSize = compressOutSize(uncompressedSize);

        compressedBuffer = jo_malloc(g_Game.compressedSize);
        if(compressedBuffer == NULL)
        {
            jo_core_error("Failed to allocate compression buffer!!");
            transitionToState(STATE_MAIN);
            return;
        }

        result = compressBuffer(g_Game.transmissionData, uncompressedSize, compressedBuffer, &g_Game.compressedSize);
        if(result != 0)
        {
            // something went wrong
            transitionToState(STATE_MAIN);
            return;
        }

        //
        // Reed Solomon encode the compressed buffer
        //
        unencodedSize = g_Game.compressedSize;
        g_Game.encodedTransmissionSize = reedSolomonOutSize(unencodedSize);

        g_Game.encodedTransmissionData = jo_malloc(g_Game.encodedTransmissionSize);
        if(g_Game.encodedTransmissionData == NULL)
        {
            jo_core_error("Failed to allocate Reed Solomon buffer!!");
            transitionToState(STATE_MAIN);
            return;
        }
        jo_memset(g_Game.encodedTransmissionData, 0, g_Game.encodedTransmissionSize);

        result = reedSolomonEncode(compressedBuffer, unencodedSize, g_Game.encodedTransmissionData);
        if(result != 0)
        {
            jo_core_error("Failed to Reed Solomon encode data!!");
            jo_free(g_Game.encodedTransmissionData);
            jo_free(compressedBuffer);
            g_Game.encodedTransmissionData = NULL;
            g_Game.encodedTransmissionSize = 0;
            transitionToState(STATE_MAIN);
            return;
        }

        // no longer need the compressed buffer
        jo_memset(compressedBuffer, 0, g_Game.compressedSize);
        jo_free(compressedBuffer);


        // escape the buffer if necessary
        result = escapeBuffer(&g_Game.encodedTransmissionData, &g_Game.encodedTransmissionSize);
        if(result != 0)
        {
            jo_core_error("Failed to escape the data!!");
            jo_free(g_Game.encodedTransmissionData);
            g_Game.encodedTransmissionData = NULL;
            g_Game.encodedTransmissionSize = 0;
            transitionToState(STATE_MAIN);
        }

        g_Game.md5Calculated = true;
    }

    // convert between internal date and jo_date
    bup_getdate(g_Game.saveDate, &jo_date);

    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Filename: %s        ", g_Game.saveFilename);
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Comment: %s         ", g_Game.saveComment);
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Date: %d/%d/%d %d:%d         ", jo_date.month, jo_date.day, jo_date.year + 1980, jo_date.time, jo_date.min);
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "MD5: %02x%02x%02x%02x%02x%02x%02x%02x", g_Game.md5Hash[0], g_Game.md5Hash[1], g_Game.md5Hash[2], g_Game.md5Hash[3], g_Game.md5Hash[4], g_Game.md5Hash[5], g_Game.md5Hash[6], g_Game.md5Hash[7]);
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "     %02x%02x%02x%02x%02x%02x%02x%02x", g_Game.md5Hash[8], g_Game.md5Hash[9], g_Game.md5Hash[10], g_Game.md5Hash[11],  g_Game.md5Hash[12], g_Game.md5Hash[13], g_Game.md5Hash[14], g_Game.md5Hash[15]);

    y++;

    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Size: %d            ", g_Game.saveFileSize);
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Compressed Size: %d            ", g_Game.compressedSize);
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Total Size: %d          ", g_Game.encodedTransmissionSize);

    result = SaturnMinimodem_transferStatus(&bytesTransferred, &totalSize);
    if(result == 0)
    {
        jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Bytes Sent: %d                ", bytesTransferred);
    }
    else
    {   // it's ok to fail, that just means our transfer hasn't startedd
        jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Bytes Sent: N/A                ");
    }

    int estimatedTimeLeft = ((g_Game.encodedTransmissionSize - bytesTransferred) * 8)/ESTIMATED_TRANSFER_SPEED;
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Est Time: %d                   ", estimatedTimeLeft);


     y++;

    if(g_Game.isTransmissionRunning == false)
    {
        jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Press C to play the data        ");
    }
    else
    {
        jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Press B to stop playing the data");

        result = SaturnMinimodem_transfer();
        if(result < 0)
        {
            jo_core_error("something went wrong!!\n");
            g_Game.isTransmissionRunning = false;
        }
        else if(result == TRANSFER_COMPLETE)
        {
            g_Game.isTransmissionRunning = false;
        }
    }

    return;
}

// handles input on the play saves screen
// B returns to the main menu
void playSaves_input(void)
{
    if(g_Game.state != STATE_PLAY_SAVES)
    {
        return;
    }

    // did the player hit start
    if(jo_is_pad1_key_pressed(JO_KEY_START) ||
       jo_is_pad1_key_pressed(JO_KEY_A) ||
       jo_is_pad1_key_pressed(JO_KEY_C))
    {
        if(g_Game.input.pressedStartAC == false)
        {
            g_Game.input.pressedStartAC = true;

            // the test is not currently running, start the test
            if(g_Game.isTransmissionRunning == false)
            {
                if(g_Game.encodedTransmissionData == NULL || g_Game.encodedTransmissionSize == 0)
                {
                    jo_core_error("Transmission data isn't initialized!!");
                    transitionToState(STATE_MAIN);
                    return;
                }

                SaturnMinimodem_initTransfer(g_Game.encodedTransmissionData, g_Game.encodedTransmissionSize);
                g_Game.isTransmissionRunning = true;
            }
            return;
        }
    }
    else
    {
        g_Game.input.pressedStartAC = false;
    }

    if(jo_is_pad1_key_pressed(JO_KEY_B))
    {
        if(g_Game.input.pressedB == false)
        {
            g_Game.input.pressedB = true;
            transitionToState(g_Game.previousState);
            return;
        }
    }
    else
    {
        g_Game.input.pressedB = false;
    }

    // update the cursor
    moveCursor(false);
    return;
}

const char* BIOS_FILENAMES[] = {"BIOS.BIN.1", "BIOS.BIN.2", "BIOS.BIN.3", "BIOS.BIN.4"};

// draws the dump bios screen
void dumpBios_draw(void)
{
    if(g_Game.state != STATE_DUMP_BIOS)
    {
        return;
    }

    jo_printf(HEADING_X, HEADING_Y, "Dump Bios");
    jo_printf(HEADING_X, HEADING_Y + 1, HEADING_UNDERSCORE);

    jo_printf(OPTIONS_X, OPTIONS_Y, "%-11s %10s", "Filename", "Bytes");
    for(int i = 0; i < 4; i++)
    {
        jo_printf(OPTIONS_X, OPTIONS_Y + i + 1, "%-11s %10d", BIOS_FILENAMES[i], BIOS_SIZE/4);
    }

    if(g_Game.md5BiosCalculated == false)
	{
		int result = 0;

		result = calculateMD5Hash(BIOS_START_ADDR, BIOS_SIZE, g_Game.md5BiosHash);
		if(result != 0)
		{
			// something went wrong
			transitionToState(STATE_MAIN);
			return;
		}

		g_Game.md5BiosCalculated = true;
	}

    jo_printf(OPTIONS_X, OPTIONS_Y + 6, "BIOS MD5: %02x%02x%02x%02x%02x%02x%02x%02x", g_Game.md5BiosHash[0], g_Game.md5BiosHash[1], g_Game.md5BiosHash[2], g_Game.md5BiosHash[3], g_Game.md5BiosHash[4], g_Game.md5BiosHash[5], g_Game.md5BiosHash[6], g_Game.md5BiosHash[7]);
    jo_printf(OPTIONS_X, OPTIONS_Y + 7, "          %02x%02x%02x%02x%02x%02x%02x%02x", g_Game.md5BiosHash[8], g_Game.md5BiosHash[9], g_Game.md5BiosHash[10], g_Game.md5BiosHash[11],  g_Game.md5BiosHash[12], g_Game.md5BiosHash[13], g_Game.md5BiosHash[14], g_Game.md5BiosHash[15]);

    // bugbug use strncpy here
    strncpy(g_Game.saveFilename, BIOS_FILENAMES[g_Game.cursorOffset], sizeof(g_Game.saveFilename) - 1);
    g_Game.saveFileSize = BIOS_SIZE/4;

    // cursor
    jo_printf(g_Game.cursorPosX, g_Game.cursorPosY + g_Game.cursorOffset, ">>");
    return;
}

// handles input on the dump bios screen
// B returns to the main menu
// Start, A, or C starts transmitting the BIOS
void dumpBios_input(void)
{
    int result = 0;

    if(g_Game.state != STATE_DUMP_BIOS)
    {
        return;
    }

    // did the player hit start
    if(jo_is_pad1_key_pressed(JO_KEY_START) ||
       jo_is_pad1_key_pressed(JO_KEY_A) ||
       jo_is_pad1_key_pressed(JO_KEY_C))
    {
        if(g_Game.input.pressedStartAC == false)
        {
            g_Game.input.pressedStartAC = true;

            result = copyBIOS(g_Game.cursorOffset);
            if(result != 0)
            {
                // something went wrong
                // copyBIOS calls jo_core_error() so we don't have to do anything
                transitionToState(STATE_MAIN);
            }

            transitionToState(STATE_PLAY_SAVES);
        }
    }
    else
    {
        g_Game.input.pressedStartAC = false;
    }

    if(jo_is_pad1_key_pressed(JO_KEY_B))
    {
        if(g_Game.input.pressedB == false)
        {
            g_Game.input.pressedB = true;
            transitionToState(STATE_MAIN);
            return;
        }
    }
    else
    {
        g_Game.input.pressedB = false;
    }

    // update the cursor
    moveCursor(false);
    return;
}

// draws the test screen
void test_draw(void)
{
    int result;
    unsigned int y = 0;

    if(g_Game.state != STATE_TEST)
    {
        return;
    }

    // heading
    jo_printf(HEADING_X, HEADING_Y + y++, "Test Audio Transmission");
    jo_printf(HEADING_X, HEADING_Y + y++, HEADING_UNDERSCORE);

    y = 0;

    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "The test sends the string:");
    y++;
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "    This ");
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "      is ");
    jo_printf(OPTIONS_X, OPTIONS_Y + y++, "      COOL");

    y++;

    if(g_Game.isTransmissionRunning == false)
    {
        jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Press C to play the test        ");
    }
    else
    {
        jo_printf(OPTIONS_X, OPTIONS_Y + y++, "Press B to stop playing the test");

        result = SaturnMinimodem_transfer();
        if(result < 0)
        {
            jo_core_error("something went wrong!!\n");
            g_Game.isTransmissionRunning = false;
        }
        else if(result == TRANSFER_COMPLETE)
        {
            g_Game.isTransmissionRunning = false;
        }
    }

    return;
}

// handles input on the credits screen
// Any button press returns back to title screen
void test_input(void)
{
    if(g_Game.state != STATE_TEST)
    {
        return;
    }

    // did the player hit start
    if(jo_is_pad1_key_pressed(JO_KEY_START) ||
       jo_is_pad1_key_pressed(JO_KEY_A) ||
       jo_is_pad1_key_pressed(JO_KEY_C))
    {
        if(g_Game.input.pressedStartAC == false)
        {
            g_Game.input.pressedStartAC = true;

            // the test is not currently running, start the test
            if(g_Game.isTransmissionRunning == false)
            {
                SaturnMinimodem_initTransfer((unsigned char*)TEST_MESSAGE, strlen(TEST_MESSAGE));
                g_Game.isTransmissionRunning = true;
            }
            return;
        }
    }
    else
    {
        g_Game.input.pressedStartAC = false;
    }

    // did the player hit b
    if(jo_is_pad1_key_pressed(JO_KEY_B))
    {
        if(g_Game.input.pressedB == false)
        {
            g_Game.input.pressedB = true;
            transitionToState(STATE_MAIN);
            return;
        }
    }
    else
    {
        g_Game.input.pressedB = false;
    }
    return;
}

// draws the save games collect screen
void collect_draw(void)
{
    unsigned int y = 0;

    if(g_Game.state != STATE_COLLECT)
    {
        return;
    }

    // heading
    jo_printf(HEADING_X, HEADING_Y + y++, "Save Game Collect Project");
    jo_printf(HEADING_X, HEADING_Y + y++, HEADING_UNDERSCORE);
    y = 0;

    // message
    jo_printf(OPTIONS_X - 3, OPTIONS_Y - 1 + y++, "Want to share your saves with");
    jo_printf(OPTIONS_X - 3, OPTIONS_Y - 1 + y++, "others?");
    y++;

    jo_printf(OPTIONS_X - 3, OPTIONS_Y - 1 + y++, "Send your .BUP files to Cafe-Alpha:");
    y++;

    jo_printf(OPTIONS_X - 3, OPTIONS_Y - 1 + y++, "ppcenter.webou.net/pskai/savedata/");
    y++;

    return;
}

// handles input on the save game collect screen
// Any button press returns back to title screen
void collect_input(void)
{
    if(g_Game.state != STATE_COLLECT)
    {
        return;
    }

    // did the player hit start
    if(jo_is_pad1_key_pressed(JO_KEY_START) ||
       jo_is_pad1_key_pressed(JO_KEY_A) ||
       jo_is_pad1_key_pressed(JO_KEY_B) ||
       jo_is_pad1_key_pressed(JO_KEY_C))
    {
        if(g_Game.input.pressedStartAC == false &&
           g_Game.input.pressedB == false)
        {
            g_Game.input.pressedStartAC = true;
            g_Game.input.pressedB = true;
            transitionToState(STATE_MAIN);
            return;
        }
    }
    else
    {
        g_Game.input.pressedStartAC = false;
        g_Game.input.pressedB = false;
    }
    return;
}

// draws the credits screen
void credits_draw(void)
{
    unsigned int y = 0;

    if(g_Game.state != STATE_CREDITS)
    {
        return;
    }

    // heading
    jo_printf(HEADING_X, HEADING_Y + y++, "Credits");
    jo_printf(HEADING_X, HEADING_Y + y++, HEADING_UNDERSCORE);
    y = 0;

    // message
    jo_printf(OPTIONS_X - 3, OPTIONS_Y - 1, "Special thanks to:");
    y++;

    jo_printf(OPTIONS_X - 3, OPTIONS_Y + y++, "Antime, Ponut, VBT, and everyone");
    jo_printf(OPTIONS_X - 3, OPTIONS_Y + y++, "else at SegaXtreme keeping the");
    jo_printf(OPTIONS_X - 3, OPTIONS_Y + y++, "Saturn dev scene alive.");
    y++;

    jo_printf(OPTIONS_X - 3, OPTIONS_Y + y++, "Thank you to Takashi for the");
    jo_printf(OPTIONS_X - 3, OPTIONS_Y + y++, "original Save Game Copier idea");
    jo_printf(OPTIONS_X - 3, OPTIONS_Y + y++, "back in ~2002.");
    y++;

    jo_printf(OPTIONS_X - 3, OPTIONS_Y + y++, "The Jo Engine and minimodem open");
    jo_printf(OPTIONS_X - 3, OPTIONS_Y + y++, "source projects.");
    y++;

    jo_printf(OPTIONS_X - 3, OPTIONS_Y + y++, "Written to help the fan translation");
    jo_printf(OPTIONS_X - 3, OPTIONS_Y + y++, "community. Can't wait to play the ");
    jo_printf(OPTIONS_X - 3, OPTIONS_Y + y++, "new translations! ");
    y++;

    jo_printf(OPTIONS_X - 3, OPTIONS_Y + y++, " - Slinga");
    jo_printf(OPTIONS_X - 3, OPTIONS_Y + y++, "(github.com/slinga-homebrew)");

    return;
}

// handles input on the credits screen
// Any button press returns back to title screen
void credits_input(void)
{
    if(g_Game.state != STATE_CREDITS)
    {
        return;
    }

    // did the player hit start
    if(jo_is_pad1_key_pressed(JO_KEY_START) ||
       jo_is_pad1_key_pressed(JO_KEY_A) ||
       jo_is_pad1_key_pressed(JO_KEY_B) ||
       jo_is_pad1_key_pressed(JO_KEY_C))
    {
        if(g_Game.input.pressedStartAC == false &&
           g_Game.input.pressedB == false)
        {
            g_Game.input.pressedStartAC = true;
            g_Game.input.pressedB = true;
            transitionToState(STATE_MAIN);
            return;
        }
    }
    else
    {
        g_Game.input.pressedStartAC = false;
        g_Game.input.pressedB = false;
    }
    return;
}

// copies the specified BIOS segment to the saveFileData buffer
int copyBIOS(unsigned int segment)
{
    if(segment > BIO_NUM_SEGMENTS)
    {
        jo_core_error("Invalid BIOS segment specified (%d)!!", segment);
        return -1;
    }

    if(g_Game.saveFileData == NULL)
    {
        jo_core_error("Save file data buffer is NULL!!");
        return -2;
    }

    if(BIOS_SEGMENT_SIZE > MAX_SAVE_SIZE)
    {
        jo_core_error("Save file data is too big!!");
        return -3;
    }

    memcpy(g_Game.saveFileData, (unsigned char*)BIOS_START_ADDR + (segment * BIOS_SEGMENT_SIZE), BIOS_SEGMENT_SIZE);
    return 0;
}

// copies the specified save gane to the saveFileData buffer
int copySaveFile(void)
{
    unsigned char* saveData = NULL;
    unsigned int saveDataSize = 0;

    if(g_Game.saveFileData == NULL)
    {
        jo_core_error("Save file data buffer is NULL!!");
        return -1;
    }

    if(g_Game.saveFileSize == 0 || g_Game.saveFileSize > MAX_SAVE_SIZE)
    {
        jo_core_error("Save file size is invalid %d!!", g_Game.saveFileSize);
        return -2;
    }

    // read the file from the backup device
    // jo engine mallocs a buffer for us
    saveDataSize = g_Game.saveFileSize;
    saveData = jo_backup_load_file_contents(g_Game.backupDevice, g_Game.saveFilename, &saveDataSize);
    if(saveData == NULL)
    {
        jo_core_error("Failed to read save file!!");
        return -3;
    }

    if(saveDataSize != g_Game.saveFileSize)
    {
        jo_core_error("Save game file size changed %d %d!!", saveDataSize, g_Game.saveFileSize);
        jo_free(saveData);
        return -4;
    }

    // copy the save game data and free the jo engine buffer
    memcpy(g_Game.saveFileData, saveData, g_Game.saveFileSize);
    jo_free(saveData);

    return 0;
}
