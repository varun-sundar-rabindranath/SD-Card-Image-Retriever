#include <iostream>
#include <vector>
#include <string> 

#include <cassert>
#include <cstdio>
#include <cstdlib>

using namespace std;

/* Search for volume ID till 100 MB from the start of the disk */
#define VOLUME_ID_SEARCH_LIMIT 100000000

static int file_count = 0;

#define MAX_NUM_BYTES 20
#define ASSUMED_BYTES_PER_SECTOR 512


typedef union char_to_int {
  int int_val;
  char char_arr_val[4];
}char_to_int; 

string interpret_as_string(FILE *fp, int offset, int num_bytes) {
  assert(num_bytes <= MAX_NUM_BYTES - 1);
  fseek(fp, offset, SEEK_SET);

  char contents[MAX_NUM_BYTES];
  /* Fill contents */
  int bytes_read = fread(&contents, sizeof(char), num_bytes, fp);
  assert(bytes_read == num_bytes);
  contents[num_bytes] = '\0';

  fseek(fp, 0, SEEK_SET);
  return string(contents);
} 

int interpret_as_int(FILE *fp, int offset, int num_bytes) {
  assert(num_bytes <= MAX_NUM_BYTES - 1);
  assert(num_bytes <= 4);
  fseek(fp, offset, SEEK_SET);

  char contents[MAX_NUM_BYTES] = {0};
  /* Fill contents */
  int bytes_read = fread(&contents, sizeof(char), num_bytes, fp);
  assert(bytes_read == num_bytes);
  contents[num_bytes] = '\0';

  char_to_int ctoi_instance;
  for(int iter = 0; iter < 4; iter++) {
    if(iter < num_bytes) {
      ctoi_instance.char_arr_val[iter] = contents[iter];
    } else {
      ctoi_instance.char_arr_val[iter] = 0;
    }
  }

  fseek(fp, 0, SEEK_SET);
  return ctoi_instance.int_val;
}
#undef MAX_NUM_BYTES

/* Print volume id information */
/* Acceptable FAT32 values refered from https://www.pjrc.com/tech/8051/ide/fat32.html */
/* Return -1 if u dont think that the sector at the offset has 
 * volume_id; If u do think that the sector at the offset has
 * volume_id return the sector ID
 */
int get_fat_volumeID_information(string dump_filename, int offset) {
  FILE *fp = fopen(dump_filename.c_str(), "r");
  bool might_contain_volume_info = false;

  /* Volume ID sector check !! */
  int num_bytes_per_sector = interpret_as_int(fp,  offset + 0x0B, 2);
  int num_sectors_per_cluster = interpret_as_int(fp,  offset + 0x0D, 1);
  int num_reserved_sectors = interpret_as_int(fp,  offset + 0x0E, 2);
  int num_fats = interpret_as_int(fp, offset + 0x10, 1);
  int num_sectors_per_fat = interpret_as_int(fp, offset + 0x24, 4);
  int root_directory_first_cluster = interpret_as_int(fp, offset + 0x2C, 4);
  int signature = interpret_as_int(fp, offset + 0x1FE, 2);
  if(signature == 43605 && 
     num_bytes_per_sector == 512 && 
     root_directory_first_cluster == 2 &&
     num_fats == 2) {
    /* This is a possible volume id sector */
    cout<<"========= sector id - "<<offset / 512<<" ============"<<endl;
    cout<<"Number of bytes  per sector  : "<<interpret_as_int(fp, offset + 0x0B, 2)<<endl;
    cout<<"Sectors per cluster          : "<<interpret_as_int(fp, offset + 0x0D, 1)<<endl;
    cout<<"Number of reserved sectors   : "<<interpret_as_int(fp, offset + 0x0E, 2)<<endl;
    cout<<"Number of FATs               : "<<interpret_as_int(fp, offset + 0x10, 1)<<endl;
    cout<<"Sectors per FAT              : "<<interpret_as_int(fp, offset + 0x24, 4)<<endl;
    cout<<"Root directory first cluster : "<<interpret_as_int(fp, offset + 0x2C, 4)<<endl;
    cout<<"Signature                    : "<<interpret_as_int(fp, offset + 0x1FE, 2)<<endl;
    cout<<"===================================="<<endl;
    might_contain_volume_info = true;
  }

  fclose(fp);

  if(might_contain_volume_info) {
    return (offset / 512);
  } else {
    return -1;
  }
}

long long int get_file_size(string dump_filename) {
  FILE *fp = fopen(dump_filename.c_str(), "r");
  fseek(fp, 0, SEEK_END);
  long long int fsize = ftell(fp);
  fclose(fp);
  return fsize;
}

/* This code was originaly written to retrieve images from
 * a memory card that had photos taken from a Canon EOS 600D;
 * It is natural that the image header contained the "Camera name"
 * information :P ( not natural ! What I did was, I got a couple of
 * Images, from an uncorrupted memory card, shot from that camera; I
 * did an "hexdump -C" on that image; Naturaly the few bytes will have
 * the image header information. I then noted down some features 
 * and the corresponding offsets that the image header has to have in all
 * the images shot using that camera!! The camera name (offset w.r.t a cluster
 * is 0xa4) was an obvious choice. So I hypothesized that any cluster
 * that had the string "Canon EOS 600D" at the offset 0xa4 is the 
 * cluster head (start of the file, duh !!) of an image.
 * To retrieve pics from someother memory card, similar hypothesis should
 * be made and the "cluster_is_image_head" function should be modified
 * accordingly !!
 */
bool cluster_is_image_head(string dump_filename, long long int cluster_offset) {
  FILE *fp = fopen(dump_filename.c_str(), "r");
  string str = interpret_as_string(fp, cluster_offset + 0xa4, 19);
  bool is_image_head = (str == "Canon EOS 600D");
  str = interpret_as_string(fp, cluster_offset + 0x606, 19);
  is_image_head |= (str == "Canon EOS 600D");
  str = interpret_as_string(fp, cluster_offset + 0x6, 4);
  is_image_head |= (str == "Exif");
  fclose(fp);
  return is_image_head;
}

/* Write clusters c_start to c_end to a file */
void write_clusters_to_file(string dump_filename, int cluster_size, int c_start, int c_end) {
  char char_arr_fname[100];
  sprintf(char_arr_fname, "./file_%d.jpg", file_count);
  file_count++;

  char *cluster_contents = NULL;
  cluster_contents = (char *)calloc(cluster_size, sizeof(char));
  assert(cluster_contents != NULL);

  FILE *fp_write = fopen(char_arr_fname, "wb+");
  FILE *fp_read = fopen(dump_filename.c_str(), "rb");
  for(int cluster_iter = c_start; cluster_iter < c_end; cluster_iter++) {
    int cluster_head_offset = cluster_iter * cluster_size;

    int num_write_bytes;
    int num_read_bytes;

    fseek(fp_read, cluster_head_offset, SEEK_SET);
    num_read_bytes = fread(cluster_contents, 1, cluster_size, fp_read);
    assert(num_read_bytes == cluster_size);

    num_write_bytes = fwrite(cluster_contents, 1, cluster_size, fp_write);
    assert(num_write_bytes == cluster_size);
  }
  fclose(fp_read);
  fclose(fp_write);
  free(cluster_contents);
}

void retrieve_image(string dump_filename, int cluster_size, vector<int> &image_head_clusters, int image_head_index) {
  /* Do nothing for now !! */
  if(image_head_index == image_head_clusters.size() - 1) {
    /* Cannot get c_end !!; Just return */
    return;
  }
  write_clusters_to_file(dump_filename, cluster_size, image_head_clusters[image_head_index], image_head_clusters[image_head_index + 1]);
  return;
}


int main(int argc, char *argv[]) {

  cerr<<"Pass memory card dump filename as an argument"<<endl;

  /* Pass memory card dump filename as argument */
  assert(argc == 2);
  string dump_filename = string(argv[1]);

  cout<<"This code will work only on FAT filesystems !! "<<endl;

  long long int file_size = get_file_size(dump_filename);

  FILE *fp = NULL;

  /* Search sector by sector for VolumeID cluster that makes sense */
  long long int init_addr = 0;
  int sector_index = -1;
  while(init_addr < file_size && sector_index == -1) {
    sector_index = get_fat_volumeID_information(dump_filename, init_addr);
    init_addr += ASSUMED_BYTES_PER_SECTOR;
  }

  if(sector_index == -1) {
    /* Could not find VOLUME ID sector; abort */
    cerr<<"Could not find VOLUME ID sector !! Abort "<<endl;
    return 0;
  }
  
  int volume_sector_id = sector_index;

  fp = fopen(dump_filename.c_str(), "r");
  int num_bytes_per_sector = interpret_as_int(fp, volume_sector_id * ASSUMED_BYTES_PER_SECTOR + 0x0B, 2);
  int num_sectors_per_cluster = interpret_as_int(fp, volume_sector_id * ASSUMED_BYTES_PER_SECTOR + 0x0D, 1);
  int num_reserved_sectors = interpret_as_int(fp, volume_sector_id * ASSUMED_BYTES_PER_SECTOR + 0x0E, 2);
  int num_fats = interpret_as_int(fp, volume_sector_id * ASSUMED_BYTES_PER_SECTOR + 0x10, 1);
  int num_sectors_per_fat = interpret_as_int(fp, volume_sector_id * ASSUMED_BYTES_PER_SECTOR + 0x24, 4);
  int root_directory_first_cluster = interpret_as_int(fp, volume_sector_id * ASSUMED_BYTES_PER_SECTOR + 0x2C, 4);
  int signature = interpret_as_int(fp, volume_sector_id * ASSUMED_BYTES_PER_SECTOR + 0x1FE, 2);
  fclose(fp);

  assert(num_bytes_per_sector == ASSUMED_BYTES_PER_SECTOR);

  /* Sanity check */
  int cluster_size = num_bytes_per_sector * num_sectors_per_cluster;
  int total_clusters = file_size / cluster_size; 

  vector<int> image_head_clusters;

  int file_count_disk_analyze = 0;
  for(int iter = 0; iter < total_clusters; iter++) {
    long long int cluster_offset = cluster_size * iter;
    if(cluster_is_image_head(dump_filename, cluster_offset)) {
      image_head_clusters.push_back(iter);
      file_count_disk_analyze++;
    }
    cerr<<"Disk analyze - "<<(int) (( ((float)iter) / ((float)total_clusters) ) * 100)<<"% \r";
  }
  cerr<<endl<<"Total files detected by disk analyze "<<file_count_disk_analyze<<endl;

  /* Lets try to retrieve the images that are recorded in the fat table */
  for(int iter = 0; iter < image_head_clusters.size(); iter++) {
    int possible_cluster_head = image_head_clusters[iter];
    retrieve_image(dump_filename, cluster_size, image_head_clusters, iter);
    cerr<<"Image "<<iter + 1<< "/"<< file_count_disk_analyze<<" retrieved "<<" \r";
  }
  cerr<<endl;

  return 0;
}
