#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "serial.h"
#include "../common/protocol.h"
#include "../firmware/version.h"
#include "commands.h"


void print_system_error(uint8_t error_code) {
  fprintf(stderr, "Error: %u\n", error_code);
}


void usage(char const* exe) {
  fprintf(stderr, "Usage: %s <serport> <command> [<args>]\n", exe);
  fprintf(stderr, "  Commands:\n");
  fprintf(stderr, "    ping\n");
  fprintf(stderr, "    id\n");
  fprintf(stderr, "    read   <startaddr> <endaddr> <outfile>\n");
  fprintf(stderr, "    write  <startaddr> <infile>\n");
  fprintf(stderr, "    verify <startaddr> <infile>\n");
  fprintf(stderr, "    crc    <startaddr> <endaddr>\n");
  fprintf(stderr, "    erase  <startaddr> <endaddr>\n");
  fprintf(stderr, "    erase chip\n");
}


int check_address_range(uint32_t start, uint32_t end) {
  int err = 0;
  if(start >= PROGRAMMER_ROM_SIZE) {
    fprintf(stderr, "Start address 0x%x is outside of chip space\n", start);
    err = 1;
  }

  if(end >= PROGRAMMER_ROM_SIZE) {
    fprintf(stderr, "End address 0x%x is outside of chip space\n", start);
    err = 1;
  }

  if(start >= end) {
    fprintf(stderr, "Cannot use negative or empty range 0x%x-0x%x\n", start, end);
    err = 1;
  }

  return err;
}


int cmd_ping(int argc, char **argv) {
  ReplyPing ping;
  uint8_t err = command_ping(&ping);

  if(err != ERROR_NONE) {
    print_system_error(err);
    return 1;
  }

  printf("Firmware version %d.%02d\n", ping.firmware_version_major, ping.firmware_version_minor);
  printf("  address bits:     %4d\n", ping.address_bits);
  printf("  max write length: %4d\n", ping.max_write_length);

  return 0;
}


int cmd_id(int argc, char **argv) {
  ReplyIdentify identify;
  uint8_t err = command_identify(&identify);

  if(err != ERROR_NONE) {
    print_system_error(err);
    return 1;
  }

  printf("EEPROM chip identification: manufacturer=0x%02X, device=0x%02X\n", identify.manufacturer_id, identify.device_id);

  return 0;
}


int cmd_read(int argc, char **argv) {
  if(argc != 3) {
    fprintf(stderr, "Invalid arguments to 'read' command\n");
    return 1;
  }

  uint32_t start = strtoul(argv[0], NULL, 0);
  uint32_t end   = strtoul(argv[1], NULL, 0);

  if(check_address_range(start, end)) {
    return 1;
  }

  FILE* outfile = fopen(argv[2], "wb");
  if(!outfile) {
    perror(argv[2]);
    return 1;
  }

  int err = 0;
  for(; start < end; start += PROGRAMMER_MAX_LEN) {
    uint8_t buf[PROGRAMMER_MAX_LEN];
    uint32_t remaining = end-start;
    uint8_t count = (remaining >= PROGRAMMER_MAX_LEN) ? PROGRAMMER_MAX_LEN : remaining;
    printf("\rReading 0x%06x-0x%06x", start, start+count-1);
    fflush(stdout);
    uint8_t err = command_read(start, count, buf);
    if(err != ERROR_NONE) {
      print_system_error(err);
      err = 1;
      goto err;
    }

    if(fwrite(buf, 1, count, outfile) != count) {
      fprintf(stderr, "Failed to write output file\n");
      err = 1;
      goto err;
    }
  }

  printf("\n");

err:
  fclose(outfile);
  return err;
}


int cmd_write(int argc, char **argv) {
  if(argc != 2) {
    fprintf(stderr, "Invalid arguments to 'write' command\n");
    return 1;
  }

  uint32_t start = strtoul(argv[0], NULL, 0);

  FILE* infile = fopen(argv[1], "rb");
  if(!infile) {
    perror(argv[2]);
    return 1;
  }

  fseek(infile, 0, SEEK_END);
  uint32_t size = (uint32_t)ftell(infile);
  rewind(infile);

  int err = 0;
  uint32_t end = start + size;
  if(check_address_range(start, end)) {
    err = 1;
    goto err;
  }

  for(; start < end; start += PROGRAMMER_MAX_LEN) {
    uint8_t buf[PROGRAMMER_MAX_LEN];
    uint32_t remaining = end-start;
    uint8_t count = (remaining >= PROGRAMMER_MAX_LEN) ? PROGRAMMER_MAX_LEN : remaining;

    if(fread(buf, 1, count, infile) != count) {
      fprintf(stderr, "Failed to read input file\n");
      err = 1;
      goto err;
    }

    printf("\rWriting 0x%06x-0x%06x", start, start+count-1);
    fflush(stdout);
    uint8_t err = command_write(start, count, buf);
    if(err != ERROR_NONE) {
      print_system_error(err);
      err = 1;
      goto err;
    }
  }

  printf("\n");

err:
  fclose(infile);
  return err;
  
}


int cmd_verify(int argc, char **argv) {

}


int cmd_crc(int argc, char **argv) {

}


int cmd_erase(int argc, char **argv) {
  if((argc >= 1) && !strcmp(argv[0], "chip")) {
    uint8_t err = command_erase_chip();

    if(err != ERROR_NONE) {
      print_system_error(err);
      return 1;
    }
    return 0;
  } else {
    if(argc >= 2) {
      uint32_t start = strtoul(argv[0], NULL, 0);
      uint32_t end   = strtoul(argv[1], NULL, 0);

      int err = 0;

      if(start & (PROGRAMMER_SECTOR_SIZE - 1) != 0) {
        fprintf(stderr, "Start address 0x%x is not at sector boundary\n", start);
        err = 1;
      }

      if(end & (PROGRAMMER_SECTOR_SIZE - 1) != 0) {
        fprintf(stderr, "End address 0x%x is not at sector boundary\n", end);
        err = 1;
      }

      err |= check_address_range(start, end);
      if(err) {
        return err;
      }
      
      uint8_t error = command_erase_sector(start, end);
      if(error) {
        print_system_error(error);
        return 1;
      }

      return 0;
    }
  }

  fprintf(stderr, "Invalid arguments to 'erase' command\n");
  return 2;
}


typedef int (*CommandFunc)(int argc, char **argv);
typedef struct {
  char const *command;
  CommandFunc func;
} Command;

static Command const commands[] = {
  {"ping",   cmd_ping},
  {"id",     cmd_id},
  {"read",   cmd_read},
  {"write",  cmd_write},
  {"verify", cmd_verify},
  {"crc",    cmd_crc},
  {"erase",  cmd_erase},
  {NULL,     NULL}
};


CommandFunc find_command(char const* command_name) {
  for(Command const* cmd = commands; cmd->command != NULL; ++cmd) {
    if(!strcmp(cmd->command, command_name)) {
      return cmd->func;
    }
  }

  return NULL;
}


int main(int argc, char **argv) {
  int error = 0;
  if(argc < 3) {
    usage(argv[0]);
    error = 1;
    goto err2;
  }

  CommandFunc func = find_command(argv[2]);
  if(func == NULL) {
    fprintf(stderr, "Unknown command '%s'\n", argv[2]);
    usage(argv[0]);
    error = 3;
    goto err2;
  }

  error = open_serial(argv[1]);
  if(error < 0) {
    error = 2;
    goto err2;
  }


  error = func(argc-3, argv+3);

err:
  close_serial();

err2:
  return error;

#if 0

    ReplyCRC crc;
    err2 = command_crc(0, 15, &crc);
    printf("CRC:\n");
    printf("  err: %4d\n", err2);
    printf("  crc: %02X\n", crc.crc);



    uint8_t data2[11] = "HALLO,FLO!";
    uint8_t err2 = command_write(2*4096-5, 10, &data2);
    printf("Write:\n");
    printf("  err: %4d\n", err2);


    int err2 = command_erase_chip();
    printf("Erase Chip:\n");
    printf("  err: %4d\n", err2);


    int err2 = command_erase_sector(4096, 2*4096);
    printf("Erase Sector:\n");
    printf("  err: %4d\n", err2);

    uint8_t data[128];
    memset(data, 0, 128);
    uint8_t err2 = command_read(4096*2-10, 128, data);
    printf("Read:\n");
    printf("  err: %d\n", err2);
    printf("  data: ");
    for(int i = 0; i < sizeof(data); ++i) {
      if(i % 16 == 0) {
        printf("\n    ");
      }
      printf("%02x ", data[i]);
    }
    printf("\n");

#endif
}
