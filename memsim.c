#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "linkedList.h"

#define FILENAME_MAX_LENGTH 64

#define PAGE_AMOUNT 1024
#define PAGE_SIZE_BYTES 64
#define VM_SIZE_BYTES PAGE_SIZE_BYTES *PAGE_AMOUNT

#define VA_SIZE_BITS 16
#define VA_OFFSET_BITS 6
#define VA_VPN_BITS VA_SIZE_BITS - VA_OFFSET_BITS
#define TWO_LEVEL_VPN_P1_BITS 5
#define TWO_LEVEL_VPN_P2_BITS VA_SIZE_BITS - VA_OFFSET_BITS - TWO_LEVEL_VPN_P1_BITS

#define PA_SIZE_BITS 16
#define PTE_SIZE_BITS 16

#define V_BIT_POSITION 15
#define R_BIT_POSITION 14
#define M_BIT_POSITION 13

#define ALGO_NAME_MAX_SIZE 6
#define MEM_REF_MAX_SIZE 15
#define ECLOCK_STEP_AMOUNT 4

// Structs
struct frame
{
    char frameData[PAGE_SIZE_BYTES];
};

// Create & Initialize Variables
struct frame *physicalMemory;
unsigned short *singlePageTable;
unsigned short *outerPageTable;
unsigned short **innerTablesTable;

// File Variables
FILE *swapFile;
FILE *referenceFile;
FILE *outputFile;

// Linked List Pointers
struct Node *lruListHead;
struct Node *lruListTail;
struct Node *circularListHead;

// (Semantically) Constant Variables
int FRAME_NUMBER;
int PFN_BIT_SIZE;
int INNER_TABLE_AMOUNT;
int INNER_TABLE_PAGE_SIZE;
int TICK;
int PAGE_OPTION;

// Variables
int initialFrameCounter;
int referenceCounter;
int totalPageFaultCounter;

// String Buffers
char ALGORITHM_NAME[ALGO_NAME_MAX_SIZE + 1];
char SWAPFILE_FILENAME[FILENAME_MAX_LENGTH];
char REFERENCE_FILENAME[FILENAME_MAX_LENGTH];
char OUTPUT_FILENAME[FILENAME_MAX_LENGTH];

unsigned short referenceExtraction(char *memoryReference, char *mode, unsigned short *value)
{
    unsigned short reference;
    sscanf(memoryReference, "%c %hx %hx", mode, &reference, value);
    return reference;
}

unsigned short extractBits(unsigned short int value, int k, int p)
{
    return (((1 << k) - 1) & (value >> p));
}

unsigned short int writeBits(unsigned short int value, int k, int p, unsigned short int newValue)
{
    unsigned short int mask = ((1 << k) - 1) << p;
    value &= ~mask;
    value |= (newValue << p) & mask;

    return value;
}

unsigned short algorithmFifo(struct Node **circularListHead, unsigned short vpn)
{
    unsigned short victimPage;

    (*circularListHead) = (*circularListHead)->prev;
    victimPage = (*circularListHead)->data;
    (*circularListHead)->data = vpn;
    return victimPage;
}

unsigned short algorithmLru(struct Node **lruListTail, unsigned short vpn)
{
    unsigned short victimPage;

    victimPage = (*lruListTail)->data;
    (*lruListTail)->data = vpn;
    return victimPage;
}

unsigned short algorithmClock(struct Node **circularListHead, unsigned short vpn, unsigned short *innerTable)
{
    unsigned short victimPage;

    // Reposition head index of circular list (not next, reverse since head is the last added)
    (*circularListHead) = (*circularListHead)->prev;

    // Find a victim page with R == 0
    while (1)
    {
        unsigned short vpnIndex = PAGE_OPTION == 1 ? (*circularListHead)->data : extractBits((*circularListHead)->data, TWO_LEVEL_VPN_P2_BITS, 0);

        if (extractBits(innerTable[vpnIndex], 1, R_BIT_POSITION) == 0)
        {
            break;
        }
        innerTable[vpnIndex] = writeBits(innerTable[vpnIndex], 1, R_BIT_POSITION, 0);
        (*circularListHead) = (*circularListHead)->prev;
    }

    victimPage = (*circularListHead)->data;
    (*circularListHead)->data = vpn;

    return victimPage;
}

unsigned short algorithmEclock(struct Node **circularListHead, unsigned short vpn, unsigned short *innerTable)
{
    unsigned short victimPage;
    unsigned short condBitR[ECLOCK_STEP_AMOUNT] = {0, 0, 0, 0};
    unsigned short condBitM[ECLOCK_STEP_AMOUNT] = {0, 1, 0, 1};

    int victimFound = 0;
    struct Node *startNode;

    // Reposition head index of circular list to tail (reverse since head is the last added)
    (*circularListHead) = (*circularListHead)->prev;
    startNode = (*circularListHead);

    // Find a victim page with ECLOCK algorithm
    for (int step = 0; step < ECLOCK_STEP_AMOUNT && !victimFound; step++)
    {
        do
        {
            unsigned short vpnIndex = PAGE_OPTION == 1 ? (*circularListHead)->data : extractBits((*circularListHead)->data, TWO_LEVEL_VPN_P2_BITS, 0);

            unsigned short bitR = extractBits(innerTable[vpnIndex], 1, R_BIT_POSITION);
            unsigned short bitM = extractBits(innerTable[vpnIndex], 1, M_BIT_POSITION);

            if (bitR == condBitR[step] && bitM == condBitM[step])
            {
                victimFound = 1;
                break;
            }

            // Reset R bits at second step
            if (step == 1)
            {
                innerTable[vpnIndex] = writeBits(innerTable[vpnIndex], 1, R_BIT_POSITION, 0);
            }

            (*circularListHead) = (*circularListHead)->prev;
        } while (startNode != (*circularListHead));
    }
    victimPage = (*circularListHead)->data;
    (*circularListHead)->data = vpn;

    return victimPage;
}

void clearReferencedBits()
{
    referenceCounter++;
    if (referenceCounter == TICK)
    {
        if (PAGE_OPTION == 1)
        {
            for (int i = 0; i < PAGE_AMOUNT; i++)
            {
                singlePageTable[i] = writeBits(singlePageTable[i], 1, R_BIT_POSITION, 0);
            }
        }
        else if (PAGE_OPTION == 2)
        {
            for (int i = 0; i < INNER_TABLE_AMOUNT; i++)
            {
                if (innerTablesTable[i] != NULL)
                {
                    for (int j = 0; j < INNER_TABLE_PAGE_SIZE; j++)
                    {
                        (innerTablesTable[i])[j] = writeBits((innerTablesTable[i])[j], 1, R_BIT_POSITION, 0);
                    }
                }
            }
        }
        referenceCounter = 0;
    }
}

void processMemoryReferences()
{
    char memoryReference[MEM_REF_MAX_SIZE];
    char mode;
    unsigned short value;
    unsigned short virtualAddress;

    unsigned short vpn;
    unsigned short vpnP1;
    unsigned short vpnP2;
    unsigned short offset;

    unsigned short physicalAddress;

    int pageFault;

    while (fgets(memoryReference, MEM_REF_MAX_SIZE, referenceFile) != NULL)
    {
        // Initially no page fault is assumes
        pageFault = 0;

        // Remove newline character from memory reference
        memoryReference[strcspn(memoryReference, "\n")] = 0;

        // Extract virtual address, mode and value from memory reference
        virtualAddress = referenceExtraction(memoryReference, &mode, &value);

        // Extract virtual page number (VPN) and offset
        vpn = extractBits(virtualAddress, VA_VPN_BITS, VA_OFFSET_BITS);
        vpnP1 = extractBits(virtualAddress, TWO_LEVEL_VPN_P1_BITS, VA_OFFSET_BITS + TWO_LEVEL_VPN_P2_BITS);
        vpnP2 = extractBits(virtualAddress, TWO_LEVEL_VPN_P2_BITS, VA_OFFSET_BITS);
        offset = extractBits(virtualAddress, VA_OFFSET_BITS, 0);

        unsigned short *innerTable;
        unsigned short vBit;
        unsigned short pfn;
        unsigned short innerTableVpnIndex;

        // Assign the page table and validation bit
        if (PAGE_OPTION == 1)
        {
            innerTable = singlePageTable;
            vBit = extractBits(singlePageTable[vpn], 1, V_BIT_POSITION);
            innerTableVpnIndex = vpn;
        }
        else if (PAGE_OPTION == 2)
        {
            unsigned short vBitP1 = extractBits(outerPageTable[vpnP1], 1, V_BIT_POSITION);

            // Create inner table if not exists
            if (vBitP1 == 0 && innerTablesTable[vpnP1] == NULL)
            {
                innerTablesTable[vpnP1] = (unsigned short *)malloc(sizeof(unsigned short) * INNER_TABLE_PAGE_SIZE);

                // Initialize inner table with all zeroes
                unsigned short zeroMask = 0x0000;
                for (int i = 0; i < INNER_TABLE_PAGE_SIZE; i++)
                {
                    (innerTablesTable[vpnP1])[i] &= zeroMask;
                }

                outerPageTable[vpnP1] = writeBits(outerPageTable[vpnP1], 1, V_BIT_POSITION, 1);
                outerPageTable[vpnP1] = writeBits(outerPageTable[vpnP1], TWO_LEVEL_VPN_P1_BITS, 0, vpnP1);
            }

            innerTable = innerTablesTable[vpnP1];
            vBit = extractBits(innerTable[vpnP2], 1, V_BIT_POSITION);
            innerTableVpnIndex = vpnP2;
        }

        // Page fault
        if (vBit == 0)
        {
            unsigned short victimPageVpn;
            unsigned short victimPageIndexVpn;
            unsigned short *victimInnerTable;

            unsigned short replacedFramePfn;
            char pageData[PAGE_SIZE_BYTES];

            // Mark page fault
            pageFault = 1;
            totalPageFaultCounter++;

            // CASE 1: Empty frame exists
            if (initialFrameCounter < FRAME_NUMBER)
            {
                // Insert the node to reference string queue depending on the used algorithm
                if (strcmp(ALGORITHM_NAME, "LRU") == 0)
                {
                    insertNode(&lruListHead, &lruListTail, vpn);
                }
                else
                {
                    circularInsertNode(&circularListHead, vpn);
                }

                replacedFramePfn = initialFrameCounter;
                initialFrameCounter++;
            }
            // CASE 2: Page replacement
            else
            {
                // Find victim page depending on the replacement algorithm
                if (strcmp(ALGORITHM_NAME, "FIFO") == 0)
                {
                    victimPageVpn = algorithmFifo(&circularListHead, vpn);
                }
                else if (strcmp(ALGORITHM_NAME, "LRU") == 0)
                {
                    victimPageVpn = algorithmLru(&lruListTail, vpn);
                }
                else if (strcmp(ALGORITHM_NAME, "CLOCK") == 0)
                {
                    victimPageVpn = algorithmClock(&circularListHead, vpn, innerTable);
                }
                else if (strcmp(ALGORITHM_NAME, "ECLOCK") == 0)
                {
                    victimPageVpn = algorithmEclock(&circularListHead, vpn, innerTable);
                }

                // Victim Table and VPN operations
                if (PAGE_OPTION == 1)
                {
                    victimInnerTable = innerTable;
                    victimPageIndexVpn = victimPageVpn;
                }
                else if (PAGE_OPTION == 2)
                {
                    unsigned short victimPageVpnP1 = extractBits(victimPageVpn, TWO_LEVEL_VPN_P1_BITS, TWO_LEVEL_VPN_P2_BITS);
                    unsigned short victimPageVpnP2 = extractBits(victimPageVpn, TWO_LEVEL_VPN_P2_BITS, 0);

                    victimInnerTable = innerTablesTable[victimPageVpnP1];
                    victimPageIndexVpn = victimPageVpnP2;
                }

                // Extract PFN of victim page
                replacedFramePfn = extractBits(victimInnerTable[victimPageIndexVpn], PFN_BIT_SIZE, 0);

                // Save the victim page to swapfile if it is modified
                unsigned short bitM = extractBits(victimInnerTable[victimPageIndexVpn], 1, M_BIT_POSITION);
                if (bitM == 1)
                {
                    fseek(swapFile, PAGE_SIZE_BYTES * victimPageVpn, SEEK_SET);
                    fwrite(physicalMemory[replacedFramePfn].frameData, PAGE_SIZE_BYTES, 1, swapFile);
                }

                // Change V bit to 0 for victim page
                victimInnerTable[victimPageIndexVpn] = writeBits(victimInnerTable[victimPageIndexVpn], 1, V_BIT_POSITION, 0);
            }

            // Page Fault Operations
            innerTable[innerTableVpnIndex] = writeBits(innerTable[innerTableVpnIndex], 1, V_BIT_POSITION, 1);
            innerTable[innerTableVpnIndex] = writeBits(innerTable[innerTableVpnIndex], 1, M_BIT_POSITION, 0);
            innerTable[innerTableVpnIndex] = writeBits(innerTable[innerTableVpnIndex], 1, R_BIT_POSITION, 1);
            innerTable[innerTableVpnIndex] = writeBits(innerTable[innerTableVpnIndex], PFN_BIT_SIZE, 0, replacedFramePfn);

            // Read the desired page data from swapfile and overwrite on the victim page's frame
            fseek(swapFile, PAGE_SIZE_BYTES * vpn, SEEK_SET);
            fread(pageData, PAGE_SIZE_BYTES, 1, swapFile);
            memcpy(physicalMemory[replacedFramePfn].frameData, pageData, PAGE_SIZE_BYTES);
        }

        // Reference operations
        if (strcmp(ALGORITHM_NAME, "LRU") == 0)
        {
            moveNodeToTop(&lruListHead, &lruListTail, vpn);
        }
        // Change R bit to 1
        innerTable[innerTableVpnIndex] = writeBits(innerTable[innerTableVpnIndex], 1, R_BIT_POSITION, 1);

        // Physical frame number (PFN) extraction
        pfn = extractBits(innerTable[innerTableVpnIndex], PFN_BIT_SIZE, 0);

        // Instruction
        if (mode == 'r')
        {
            // Read operations
            // physicalMemory[pfn].frameData[offset];
        }
        else if (mode == 'w')
        {
            // Write operations
            physicalMemory[pfn].frameData[offset] = (char)value;

            // Change M bit to 1
            innerTable[innerTableVpnIndex] = writeBits(innerTable[innerTableVpnIndex], 1, M_BIT_POSITION, 1);
        }

        // Export reference log to output file
        physicalAddress = writeBits(virtualAddress, VA_VPN_BITS, VA_OFFSET_BITS, pfn);
        fprintf(outputFile, "0x%04hx 0x%hx 0x%hx 0x%hx 0x%hx 0x%04hx%s\n",
                virtualAddress, PAGE_OPTION == 1 ? vpn : vpnP1,
                PAGE_OPTION == 1 ? 0 : vpnP2,
                offset,
                pfn,
                physicalAddress,
                pageFault == 1 ? " pgfault" : " ");

        // Increase referenceCounter and reset R bits if needed
        clearReferencedBits();
    }
}

void memoryFlush()
{
    if (PAGE_OPTION == 1)
    {
        unsigned short extractedFrame;

        for (int i = 0; i < PAGE_AMOUNT; i++)
        {
            if (extractBits(singlePageTable[i], 1, V_BIT_POSITION) == 1)
            {
                extractedFrame = extractBits(singlePageTable[i], PFN_BIT_SIZE, 0);

                fseek(swapFile, PAGE_SIZE_BYTES * i, SEEK_SET);
                fwrite(physicalMemory[extractedFrame].frameData, PAGE_SIZE_BYTES, 1, swapFile);
            }
        }
    }
    else if (PAGE_OPTION == 2)
    {
        unsigned short tmpVirtualAddress = 0x0000;
        unsigned short extractedVpn;
        unsigned short extractedFrame;
        for (int i = 0; i < INNER_TABLE_AMOUNT; i++)
        {
            if (extractBits(outerPageTable[i], 1, V_BIT_POSITION) == 1)
            {
                // Write VPN_P1
                tmpVirtualAddress = writeBits(tmpVirtualAddress, TWO_LEVEL_VPN_P1_BITS, TWO_LEVEL_VPN_P2_BITS + VA_OFFSET_BITS, i);
                for (int j = 0; j < INNER_TABLE_PAGE_SIZE; j++)
                {
                    if (innerTablesTable[i] != NULL)
                    {
                        // Write VPN_P2
                        tmpVirtualAddress = writeBits(tmpVirtualAddress, TWO_LEVEL_VPN_P2_BITS, VA_OFFSET_BITS, j);
                        if (extractBits((innerTablesTable[i])[j], 1, V_BIT_POSITION) == 1)
                        {
                            extractedVpn = extractBits(tmpVirtualAddress, VA_VPN_BITS, VA_OFFSET_BITS);
                            extractedFrame = extractBits((innerTablesTable[i])[j], PFN_BIT_SIZE, 0);

                            fseek(swapFile, PAGE_SIZE_BYTES * extractedVpn, SEEK_SET);
                            fwrite(physicalMemory[extractedFrame].frameData, PAGE_SIZE_BYTES, 1, swapFile);
                        }
                    }
                }
            }
        }
    }
}

int main(int argc, char *argv[])
{
    int option;
    while ((option = getopt(argc, argv, "p:r:s:f:a:t:o:")) != -1)
    {
        switch (option)
        {
        case 'p':
            PAGE_OPTION = atoi(optarg);
            if (PAGE_OPTION < 1 || PAGE_OPTION > 2)
            {
                fprintf(stderr, "Error: Minimum and maximum values for page level are 1 and 2.\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'r':
            if (optarg == NULL || strcmp(optarg, "") == 0 || optarg[0] == '-')
            {
                fprintf(stderr, "Error: Address file name must not be NULL.\n");
                exit(EXIT_FAILURE);
            }
            strcpy(REFERENCE_FILENAME, optarg);
            break;
        case 's':
            if (optarg == NULL || strcmp(optarg, "") == 0 || optarg[0] == '-')
            {
                fprintf(stderr, "Error: Swap file name must not be NULL.\n");
                exit(EXIT_FAILURE);
            }
            strcpy(SWAPFILE_FILENAME, optarg);
            break;
        case 'f':
            FRAME_NUMBER = atoi(optarg);
            if (FRAME_NUMBER < 4 || FRAME_NUMBER > 128)
            {
                fprintf(stderr, "Error: Minimum and maximum values for page level are 4 and 128.\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'a':
            if (optarg == NULL || strcmp(optarg, "") == 0 || optarg[0] == '-')
            {
                fprintf(stderr, "Error: Algorithm name must not be NULL.\n");
                exit(EXIT_FAILURE);
            }
            strcpy(ALGORITHM_NAME, optarg);
            break;
        case 't':
            TICK = atoi(optarg);
            if (TICK < 0)
            {
                fprintf(stderr, "Error: Minimum value for tick is 0.\n");
                exit(EXIT_FAILURE);
            }
            break;
        case 'o':
            if (optarg == NULL || strcmp(optarg, "") == 0 || optarg[0] == '-')
            {
                fprintf(stderr, "Error: Output file name must not be NULL.\n");
                exit(EXIT_FAILURE);
            }
            strcpy(OUTPUT_FILENAME, optarg);
            break;
        default:
            fprintf(stderr, "Usage: %s -p level -r addrfile -s swapfile -f fcount -a algo -t tick -o outfile\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    PFN_BIT_SIZE = (int)ceil(log2((double)FRAME_NUMBER));
    INNER_TABLE_AMOUNT = (int)pow(2, TWO_LEVEL_VPN_P1_BITS);
    INNER_TABLE_PAGE_SIZE = (int)pow(2, TWO_LEVEL_VPN_P2_BITS);

    lruListHead = NULL;
    lruListTail = NULL;
    circularListHead = NULL;

    initialFrameCounter = 0;
    referenceCounter = 0;
    totalPageFaultCounter = 0;

    physicalMemory = (struct frame *)malloc(PAGE_SIZE_BYTES * FRAME_NUMBER);
    singlePageTable = (unsigned short *)malloc(sizeof(unsigned short) * PAGE_AMOUNT);
    outerPageTable = (unsigned short *)malloc(sizeof(unsigned short) * INNER_TABLE_AMOUNT);
    innerTablesTable = (unsigned short **)malloc(sizeof(unsigned short *) * INNER_TABLE_AMOUNT);

    // Initialize Swap File
    swapFile = fopen(SWAPFILE_FILENAME, "r+");
    if (swapFile == NULL)
    {
        swapFile = fopen(SWAPFILE_FILENAME, "w+");
        if (swapFile == NULL)
        {
            perror("fopen");
            exit(1);
        }

        char intialData[VM_SIZE_BYTES];

        for (int i = 0; i < VM_SIZE_BYTES; i++)
        {
            intialData[i] = 0;
        }

        fwrite(intialData, sizeof(intialData), 1, swapFile);
    }

    // Initialize Single Page Table
    unsigned short zeroMask = 0x0000;
    for (int i = 0; i < PAGE_AMOUNT; i++)
    {
        singlePageTable[i] &= zeroMask;
    }

    // Initialize Outer Page Table
    for (int i = 0; i < INNER_TABLE_AMOUNT; i++)
    {
        outerPageTable[i] &= zeroMask;
    }

    // Assign Inner Tables Table to NULL
    for (int i = 0; i < INNER_TABLE_AMOUNT; i++)
    {
        innerTablesTable[i] = NULL;
    }

    // Open Reference File
    referenceFile = fopen(REFERENCE_FILENAME, "r+");
    if (referenceFile == NULL)
    {
        perror("fopen");
        exit(1);
    }

    // Open Output File
    outputFile = fopen(OUTPUT_FILENAME, "w+");
    if (outputFile == NULL)
    {
        perror("fopen");
        exit(1);
    }

    // Process all memory references in the address file
    processMemoryReferences();

    // Flush all valid table entries' corresponding frames to the swapfile
    memoryFlush();

    // Write total page fault count to the output file
    fprintf(outputFile, "\n TOTAL NUMBER OF PAGE FAULTS: %d\n", totalPageFaultCounter);

    // Clean Up
    for (int i = 0; i < INNER_TABLE_AMOUNT; i++)
    {
        if (innerTablesTable[i] != NULL)
        {
            free(innerTablesTable[i]);
        }
    }

    free(physicalMemory);
    free(singlePageTable);
    free(outerPageTable);
    free(innerTablesTable);

    fclose(swapFile);
    fclose(referenceFile);
    fclose(outputFile);
}