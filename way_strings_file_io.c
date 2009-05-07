#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "file_types.h"
#include "way_strings_file_io.h"
#include "raw_file_db.h"
#include "script_datatypes.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

using namespace std;

typedef char int8;
typedef short int int16;
typedef int int32;
typedef long long int64;

typedef unsigned char uint8;
typedef unsigned short int uint16;
typedef unsigned int uint32;
typedef unsigned long long uint64;

//-----------------------------------------------------------------------------

const string WAY_STRING_DATA("way_strings.dat");
const string WAY_STRING_IDX("way_strings.idx");

const char* WAY_TAG_TMPAPREFIX = "/tmp/way_strings_";
const char* WAY_TAG_TMPB = "/tmp/way_tag_ids";

//-----------------------------------------------------------------------------

uint32 WayCollection::next_way_tag_id(0);

void flush_way_tags(uint& current_run, map< WayKeyValue, WayCollection >& way_tags, vector< Way_ >& ways)
{
  //determine whether a certain tag exists only local
  sort(ways.begin(), ways.end(), Way_Less_By_Id());
  for (map< WayKeyValue, WayCollection >::iterator it(way_tags.begin());
       it != way_tags.end(); ++it)
  {
    uint32 position(lower_bound
	(ways.begin(), ways.end(), Way_(0, *(it->second.ways.begin())), Way_Less_By_Id())->head.first);
    uint32 bitmask(0);
    for (vector< uint32 >::iterator it2(it->second.ways.begin()); it2 != it->second.ways.end(); ++it2)
    {
      bitmask |= (position ^ (lower_bound
	  (ways.begin(), ways.end(), Way_(0, *it2), Way_Less_By_Id())->head.first));
    }
    it->second.position = position;
    it->second.bitmask = bitmask;
  }
  
  ostringstream temp;
  temp<<WAY_TAG_TMPAPREFIX<<current_run;
  int dest_fd = open64(temp.str().c_str(), O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  close(dest_fd);
  
  dest_fd = open64(temp.str().c_str(), O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  if (dest_fd < 0)
    throw File_Error(errno, temp.str().c_str(), "flush_way_tags:1");
  
  if (current_run == 0)
  {
    int dest_id_fd = open64(WAY_TAG_TMPB, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (dest_id_fd < 0)
      throw File_Error(errno, WAY_TAG_TMPB, "flush_way_tags:2");
    
    for (map< WayKeyValue, WayCollection >::iterator it(way_tags.begin());
         it != way_tags.end(); ++it)
    {
      uint16 key_size(it->first.key.size());
      uint16 value_size(it->first.value.size());
      if (it->second.bitmask>>8)
        it->second.position = 0xffffffff;
      else
        it->second.position &= 0xffffff00;
      write(dest_fd, &(it->second.id), sizeof(uint32));
      write(dest_fd, &(it->second.position), sizeof(uint32));
      write(dest_fd, &(key_size), sizeof(uint16));
      write(dest_fd, &(value_size), sizeof(uint16));
      write(dest_fd, &(it->first.key[0]), key_size);
      write(dest_fd, &(it->first.value[0]), value_size);
      
      uint32 nc_size(it->second.ways.size());
      write(dest_id_fd, &(it->second.id), sizeof(uint32));
      write(dest_id_fd, &(nc_size), sizeof(uint32));
      for (vector< uint32 >::const_iterator it2(it->second.ways.begin());
	   it2 != it->second.ways.end(); ++it2)
	write(dest_id_fd, &(*it2), sizeof(uint32));
    }
    
    close(dest_id_fd);
  }
  else
  {
    uint32* cnt_rd_buf = (uint32*) malloc(2*sizeof(uint32) + 2*sizeof(uint16));
    uint16* size_rd_buf = (uint16*) &(cnt_rd_buf[2]);
    char* key_rd_buf = (char*) malloc(64*1024);
    char* value_rd_buf = (char*) malloc(64*1024);
    
    temp.str("");
    temp<<WAY_TAG_TMPAPREFIX<<(current_run-1);
    int source_fd = open64(temp.str().c_str(), O_RDONLY);
    if (source_fd < 0)
      throw File_Error(errno, temp.str().c_str(), "flush_way_tags:3");
    
    map< WayKeyValue, WayCollection >::iterator it(way_tags.begin());
    while (read(source_fd, cnt_rd_buf, 2*sizeof(uint32) + 2*sizeof(uint16)))
    {
      read(source_fd, key_rd_buf, size_rd_buf[0]);
      key_rd_buf[size_rd_buf[0]] = 0;
      read(source_fd, value_rd_buf, size_rd_buf[1]);
      value_rd_buf[size_rd_buf[1]] = 0;
      while ((it != way_tags.end()) &&
             (strcmp(key_rd_buf, it->first.key.c_str()) > 0))
      {
        uint16 key_size(it->first.key.size());
        uint16 value_size(it->first.value.size());
        if (it->second.bitmask>>8)
          it->second.position = 0xffffffff;
        else
          it->second.position &= 0xffffff00;
        write(dest_fd, &(it->second.id), sizeof(uint32));
        write(dest_fd, &(it->second.position), sizeof(uint32));
        write(dest_fd, &(key_size), sizeof(uint16));
        write(dest_fd, &(value_size), sizeof(uint16));
        write(dest_fd, &(it->first.key[0]), key_size);
        write(dest_fd, &(it->first.value[0]), value_size);
        ++it;
      }
      while ((it != way_tags.end()) &&
             (strcmp(key_rd_buf, it->first.key.c_str()) == 0) &&
             (strcmp(value_rd_buf, it->first.value.c_str()) > 0))
      {
        uint16 key_size(it->first.key.size());
        uint16 value_size(it->first.value.size());
        if (it->second.bitmask>>8)
          it->second.position = 0xffffffff;
        else
          it->second.position &= 0xffffff00;
        write(dest_fd, &(it->second.id), sizeof(uint32));
        write(dest_fd, &(it->second.position), sizeof(uint32));
        write(dest_fd, &(key_size), sizeof(uint16));
        write(dest_fd, &(value_size), sizeof(uint16));
        write(dest_fd, &(it->first.key[0]), key_size);
        write(dest_fd, &(it->first.value[0]), value_size);
        ++it;
      }
      if ((it != way_tags.end()) &&
          (strcmp(key_rd_buf, it->first.key.c_str()) == 0) &&
          (strcmp(value_rd_buf, it->first.value.c_str()) == 0))
      {
        it->second.merge(cnt_rd_buf[0], cnt_rd_buf[1]);
        if ((cnt_rd_buf[1] == 0xffffffff) || (it->second.bitmask>>8))
          cnt_rd_buf[1] = 0xffffffff;
        else
          cnt_rd_buf[1] &= 0xffffff00;
        ++it;
      }
      write(dest_fd, cnt_rd_buf, 2*sizeof(uint32) + 2*sizeof(uint16));
      write(dest_fd, key_rd_buf, size_rd_buf[0]);
      write(dest_fd, value_rd_buf, size_rd_buf[1]);
    }
    
    free(cnt_rd_buf);
    free(key_rd_buf);
    free(value_rd_buf);
    
    close(source_fd);
    temp.str("");
    temp<<WAY_TAG_TMPAPREFIX<<(current_run-1);
    remove(temp.str().c_str());
    
    int dest_id_fd = open64(WAY_TAG_TMPB, O_WRONLY|O_APPEND, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
    if (dest_id_fd < 0)
      throw File_Error(errno, WAY_TAG_TMPB, "flush_way_tags:4");
    
    for (map< WayKeyValue, WayCollection >::iterator it(way_tags.begin());
	 it != way_tags.end(); ++it)
    {
      uint32 nc_size(it->second.ways.size());
      write(dest_id_fd, &(it->second.id), sizeof(uint32));
      write(dest_id_fd, &(nc_size), sizeof(uint32));
      for (vector< uint32 >::const_iterator it2(it->second.ways.begin());
	   it2 != it->second.ways.end(); ++it2)
	write(dest_id_fd, &(*it2), sizeof(uint32));	
    }
    
    close(dest_id_fd);
  }
  
  close(dest_fd);
  ++current_run;
}

void way_tag_statistics(uint& current_run, vector< uint32 >& split_idx)
{
  cerr<<'s';
  
  if (current_run == 0)
  {
    cerr<<"No way tags.\n";
    return;
  }
  
  ostringstream temp;
  temp<<WAY_TAG_TMPAPREFIX<<(current_run-1);
  int source_fd = open64(temp.str().c_str(), O_RDONLY);
  if (source_fd < 0)
    throw File_Error(errno, temp.str().c_str(), "way_tag_statistics:1");
  
  uint32 global_count(0);
  uint32* spatial_count = (uint32*) calloc(16*1024*1024, sizeof(uint32));
  uint32* cnt_rd_buf = (uint32*) malloc(2*sizeof(uint32) + 2*sizeof(uint16));
  uint16* size_rd_buf = (uint16*) &(cnt_rd_buf[2]);
  
  while (read(source_fd, cnt_rd_buf, 2*sizeof(uint32) + 2*sizeof(uint16)))
  {
    lseek64(source_fd, size_rd_buf[0] + size_rd_buf[1], SEEK_CUR);
    ++(spatial_count[cnt_rd_buf[1]>>8]);
    ++global_count;
  }
  global_count -= spatial_count[0x00ffffff];
  
  split_idx.clear();
  uint32 split_count(0);
  for (unsigned int i(0); i < 16*1024*1024; ++i)
  {
    split_count += spatial_count[i];
    if (split_count >= global_count / WAY_TAG_SPATIAL_PARTS)
    {
      split_idx.push_back(i<<8);
      split_count -= global_count / WAY_TAG_SPATIAL_PARTS;
    }
  }
  while (split_idx.size() < WAY_TAG_SPATIAL_PARTS)
    split_idx.push_back(0xffffff<<8);
  split_idx[WAY_TAG_SPATIAL_PARTS-1] = 0xffffff<<8;
  
  free(cnt_rd_buf);
  free(spatial_count);
  
  close(source_fd);

  cerr<<'s';
}

void way_tag_split_and_index(uint& current_run, vector< uint32 >& split_idx, uint32*& block_of_id)
{
  cerr<<'p';

  ostringstream temp;
  temp<<WAY_TAG_TMPAPREFIX<<(current_run-1);
  int source_fd = open64(temp.str().c_str(), O_RDONLY);
  if (source_fd < 0)
    throw File_Error(errno, temp.str().c_str(), "way_tag_split_and_index:1");
  
  int dest_fd = open64((DATADIR + db_subdir + WAY_STRING_DATA).c_str(),
                       O_WRONLY|O_CREAT|O_TRUNC,
		       S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  close(dest_fd);
  
  dest_fd = open64((DATADIR + db_subdir + WAY_STRING_DATA).c_str(),
                   O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  if (dest_fd < 0)
    throw File_Error(errno, DATADIR + db_subdir + WAY_STRING_DATA,
                     "way_tag_split_and_index:2");
  
  int dest_idx_fd = open64((DATADIR + db_subdir + WAY_STRING_IDX).c_str(),
                           O_WRONLY|O_CREAT|O_TRUNC,
			   S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  close(dest_idx_fd);
  
  dest_idx_fd = open64((DATADIR + db_subdir + WAY_STRING_IDX).c_str(),
                       O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  if (dest_fd < 0)
    throw File_Error(errno, DATADIR + db_subdir + WAY_STRING_IDX, "way_tag_split_and_index:3");
  
  write(dest_idx_fd, &(WayCollection::next_way_tag_id), sizeof(uint32));
  for (uint32 i(0); i < WAY_TAG_SPATIAL_PARTS; ++i)
    write(dest_idx_fd, &(split_idx[i]), sizeof(uint32));
  
  uint8* cur_block_count = (uint8*) calloc(WAY_TAG_SPATIAL_PARTS+1, sizeof(uint8));
  block_of_id = (uint32*) calloc((WayCollection::next_way_tag_id+1), sizeof(uint32));
  
  uint32* write_pos = (uint32*) malloc((WAY_TAG_SPATIAL_PARTS+1)*sizeof(uint32));
  for (uint32 i(0); i < WAY_TAG_SPATIAL_PARTS+1; ++i)
    write_pos[i] = sizeof(uint32);
  char* write_blocks = (char*) malloc((WAY_TAG_SPATIAL_PARTS+1) * WAY_STRING_BLOCK_SIZE);
  uint32* cnt_rd_buf = (uint32*) malloc(2*sizeof(uint32) + 2*sizeof(uint16));
  uint16* size_rd_buf = (uint16*) &(cnt_rd_buf[2]);
  
  while (read(source_fd, cnt_rd_buf, 2*sizeof(uint32) + 2*sizeof(uint16)))
  {
    uint16 block(0);
    if (cnt_rd_buf[1] != 0xffffffff)
    {
      while (split_idx[block] <= cnt_rd_buf[1])
	++block;
      ++block;
    }
    
    if (write_pos[block] + 2*sizeof(uint32) + 2*sizeof(uint16) + size_rd_buf[0] + size_rd_buf[1]
	>= WAY_STRING_BLOCK_SIZE*4/5)
    {
      *((uint32*)&(write_blocks[block * WAY_STRING_BLOCK_SIZE])) = write_pos[block] - sizeof(uint32);
      write(dest_fd, &(write_blocks[block * WAY_STRING_BLOCK_SIZE]), WAY_STRING_BLOCK_SIZE);
      write(dest_idx_fd, &(block), sizeof(uint16));
      write(dest_idx_fd, &(write_blocks[block * WAY_STRING_BLOCK_SIZE
	  + 3*sizeof(uint32)]), *(uint16*)&(write_blocks[block * WAY_STRING_BLOCK_SIZE
	      + 3*sizeof(uint32)]) + *(uint16*)&(write_blocks[block * WAY_STRING_BLOCK_SIZE
		  + 3*sizeof(uint32) + sizeof(uint16)]) + 2*sizeof(uint16));
    
      write_pos[block] = sizeof(uint32);

      memcpy(&(write_blocks[block * WAY_STRING_BLOCK_SIZE + write_pos[block]]), cnt_rd_buf,
	       2*sizeof(uint32) + 2*sizeof(uint16));
      write_pos[block] += 2*sizeof(uint32) + 2*sizeof(uint16);
      read(source_fd, &(write_blocks[block * WAY_STRING_BLOCK_SIZE + write_pos[block]]),
	   size_rd_buf[0] + size_rd_buf[1]);
      write_pos[block] += size_rd_buf[0] + size_rd_buf[1];
      
      block_of_id[cnt_rd_buf[0]] = (++cur_block_count[block]) | ((cnt_rd_buf[1]) & (0xffffff00));
    }
    else
    {
      memcpy(&(write_blocks[block * WAY_STRING_BLOCK_SIZE + write_pos[block]]), cnt_rd_buf,
	       2*sizeof(uint32) + 2*sizeof(uint16));
      write_pos[block] += 2*sizeof(uint32) + 2*sizeof(uint16);
      read(source_fd, &(write_blocks[block * WAY_STRING_BLOCK_SIZE + write_pos[block]]),
	   size_rd_buf[0] + size_rd_buf[1]);
      write_pos[block] += size_rd_buf[0] + size_rd_buf[1];
    
      block_of_id[cnt_rd_buf[0]] = (cur_block_count[block]) | ((cnt_rd_buf[1]) & (0xffffff00));
    }
  }
  
  for (unsigned int i(0); i < WAY_TAG_SPATIAL_PARTS+1; ++i)
  {
    *((uint32*)&(write_blocks[i * WAY_STRING_BLOCK_SIZE])) = write_pos[i] - sizeof(uint32);
    write(dest_fd, &(write_blocks[i * WAY_STRING_BLOCK_SIZE]), WAY_STRING_BLOCK_SIZE);
    write(dest_idx_fd, &(i), sizeof(uint16));
    write(dest_idx_fd, &(write_blocks[i * WAY_STRING_BLOCK_SIZE
	+ 3*sizeof(uint32)]), *(uint16*)&(write_blocks[i * WAY_STRING_BLOCK_SIZE
	    + 3*sizeof(uint32)]) + *(uint16*)&(write_blocks[i * WAY_STRING_BLOCK_SIZE
		+ 3*sizeof(uint32) + sizeof(uint16)]) + 2*sizeof(uint16));
  }
  
  free(cur_block_count);
  free(write_pos);
  free(write_blocks);
  free(cnt_rd_buf);
  
  close(source_fd);
  close(dest_fd);

  cerr<<'p';
}

struct tag_id_local_less : public binary_function< uint32*, uint32*, bool >
{
  tag_id_local_less(uint32* block_of_id) : block_of_id_(block_of_id) {}

  bool operator() (uint32* const& a, uint32* const& b)
  {
    if ((block_of_id_[*a] & 0xffffff00) < (block_of_id_[*b] & 0xffffff00))
      return true;
    else if ((block_of_id_[*a] & 0xffffff00) > (block_of_id_[*b] & 0xffffff00))
      return false;
    return (*a < *b);
  }
private:
  uint32* block_of_id_;
};

struct tag_id_global_less : public binary_function< uint32*, uint32*, bool >
{
  bool operator() (uint32* const& a, uint32* const& b)
  {
    return (*a < *b);
  }
};

void way_tag_create_id_way_idx(uint32* block_of_id)
{
  uint32 rd_buf_pos(0);
  Tag_Id_Way_Local_Writer env_local(block_of_id);
  Tag_Id_Way_Global_Writer env_global;
  
  int source_fd = open64(WAY_TAG_TMPB, O_RDONLY);
  if (source_fd < 0)
    throw File_Error(errno, WAY_TAG_TMPB, "way_tag_create_id_way_idx:1");
  
  uint32* tag_rd_buf = (uint32*) malloc(WAY_TAG_SORT_BUFFER_SIZE);
  uint32* tag_alt_buf = (uint32*) malloc(WAY_TAG_SORT_BUFFER_SIZE);
  uint32 max_pos(0);
  
  while ((max_pos =
	 read(source_fd, &(tag_rd_buf[rd_buf_pos]), WAY_TAG_SORT_BUFFER_SIZE - rd_buf_pos*sizeof(uint32))))
  {
    vector< uint32* > tag_id_local, tag_id_global;
    uint32 alt_buf_pos(0);
    max_pos += rd_buf_pos*sizeof(uint32);
    rd_buf_pos = 0;
  
    while ((rd_buf_pos + 1 < max_pos / sizeof(uint32)) &&
	    (rd_buf_pos + tag_rd_buf[rd_buf_pos+1] + 1 < max_pos / sizeof(uint32)))
    {
      uint32 next_pos(tag_rd_buf[rd_buf_pos+1] + 2);
      
      if ((~(block_of_id[tag_rd_buf[rd_buf_pos]])) & 0xffffff00)
      {
	tag_id_local.push_back(&(tag_rd_buf[rd_buf_pos]));
      
	while (tag_rd_buf[rd_buf_pos+1] > 255)
	{
	  if (tag_rd_buf[rd_buf_pos+1] > 510)
	  {
	    tag_id_local.push_back(&(tag_alt_buf[alt_buf_pos]));
	    tag_id_local.push_back(&(tag_rd_buf[rd_buf_pos + 510]));
	  
	    tag_alt_buf[alt_buf_pos++] = tag_rd_buf[rd_buf_pos];
	    tag_alt_buf[alt_buf_pos++] = 255;
	    memcpy(&(tag_alt_buf[alt_buf_pos]), &(tag_rd_buf[rd_buf_pos+257]), 255*sizeof(uint32));
	    alt_buf_pos += 255;
	    tag_rd_buf[rd_buf_pos+1] = 255;
	  
	    next_pos -= 510;
	    rd_buf_pos += 510;
	  
	    tag_rd_buf[rd_buf_pos] = tag_rd_buf[rd_buf_pos - 510];
	    tag_rd_buf[rd_buf_pos+1] = next_pos - 2;
	  }
	  else
	  {
	    tag_id_local.push_back(&(tag_alt_buf[alt_buf_pos]));
	  
	    tag_alt_buf[alt_buf_pos++] = tag_rd_buf[rd_buf_pos];
	    tag_alt_buf[alt_buf_pos++] = tag_rd_buf[rd_buf_pos+1] - 255;
	    memcpy(&(tag_alt_buf[alt_buf_pos]), &(tag_rd_buf[rd_buf_pos+257]),
		     (tag_rd_buf[rd_buf_pos+1] - 255)*sizeof(uint32));
	    alt_buf_pos += (tag_rd_buf[rd_buf_pos+1] - 255);
	    tag_rd_buf[rd_buf_pos+1] = 255;
	  }
	}
      }
      else
      {
	tag_id_global.push_back(&(tag_rd_buf[rd_buf_pos]));
      
	while (tag_rd_buf[rd_buf_pos+1] > 255)
	{
	  if (tag_rd_buf[rd_buf_pos+1] > 510)
	  {
	    tag_id_global.push_back(&(tag_alt_buf[alt_buf_pos]));
	    tag_id_global.push_back(&(tag_rd_buf[rd_buf_pos + 510]));
	  
	    tag_alt_buf[alt_buf_pos++] = tag_rd_buf[rd_buf_pos];
	    tag_alt_buf[alt_buf_pos++] = 255;
	    memcpy(&(tag_alt_buf[alt_buf_pos]), &(tag_rd_buf[rd_buf_pos+257]), 255*sizeof(uint32));
	    alt_buf_pos += 255;
	    tag_rd_buf[rd_buf_pos+1] = 255;
	  
	    next_pos -= 510;
	    rd_buf_pos += 510;
	  
	    tag_rd_buf[rd_buf_pos] = tag_rd_buf[rd_buf_pos - 510];
	    tag_rd_buf[rd_buf_pos+1] = next_pos - 2;
	  }
	  else
	  {
	    tag_id_global.push_back(&(tag_alt_buf[alt_buf_pos]));
	  
	    tag_alt_buf[alt_buf_pos++] = tag_rd_buf[rd_buf_pos];
	    tag_alt_buf[alt_buf_pos++] = tag_rd_buf[rd_buf_pos+1] - 255;
	    memcpy(&(tag_alt_buf[alt_buf_pos]), &(tag_rd_buf[rd_buf_pos+257]),
		     (tag_rd_buf[rd_buf_pos+1] - 255)*sizeof(uint32));
	    alt_buf_pos += (tag_rd_buf[rd_buf_pos+1] - 255);
	    tag_rd_buf[rd_buf_pos+1] = 255;
	  }
	}
      }
      
      rd_buf_pos += next_pos;
    }
    
    sort(tag_id_local.begin(), tag_id_local.end(), tag_id_local_less(block_of_id));
    sort(tag_id_global.begin(), tag_id_global.end(), tag_id_global_less());
    
    flush_data< Tag_Id_Way_Local_Writer >
	(env_local, tag_id_local.begin(), tag_id_local.end());
    flush_data< Tag_Id_Way_Global_Writer >
        (env_global, tag_id_global.begin(), tag_id_global.end());
    
    cerr<<'.';
    
    memmove(tag_rd_buf, &(tag_rd_buf[rd_buf_pos]), WAY_TAG_SORT_BUFFER_SIZE - rd_buf_pos*sizeof(uint32));
    rd_buf_pos = WAY_TAG_SORT_BUFFER_SIZE / sizeof(uint32) - rd_buf_pos;
  }
  
  make_block_index< Tag_Id_Way_Local_Writer >(env_local);
  make_block_index< Tag_Id_Way_Global_Writer >(env_global);
  
  free(tag_rd_buf);
  free(tag_alt_buf);
  
  close(source_fd);
}

struct way_idx_less : public binary_function< uint32, uint32, bool >
{
  way_idx_less(uint32* ll_idx__) : ll_idx_(ll_idx__) {}
  
  bool operator() (const uint32& a, const uint32& b)
  {
    return (ll_idx_[a] < ll_idx_[b]);
  }
  
  private:
    uint32* ll_idx_;
};

void way_tag_create_way_id_idx(uint32* block_of_id, uint32 max_way_id)
{
  const uint32 max_ways_ram = 32*1024*1024;
  
  int source_fd = open64(WAY_TAG_TMPB, O_RDONLY);
  if (source_fd < 0)
    throw File_Error(errno, WAY_TAG_TMPB, "way_tag_create_id_way_idx:2");
  
  uint8* blocklet_of_id = (uint8*) malloc((WayCollection::next_way_tag_id+1)*sizeof(uint8));
  for (uint32 i(0); i < (WayCollection::next_way_tag_id+1); ++i)
  {
    if ((block_of_id[i] & 0xffffff00) == 0xffffff00)
      blocklet_of_id[i] = (block_of_id[i] & 0xff);
    else
      blocklet_of_id[i] = 0xff;
  }
  free(block_of_id);
  
  uint32 count(max_ways_ram);
  uint32* ll_idx_ = (uint32*) malloc(sizeof(uint32)*count);
  
  Tag_Way_Id_Writer env(ll_idx_, blocklet_of_id);
  env.offset = 1;
  
  while (env.offset < max_way_id)
  {
    env.ids_of_way.clear();
    env.ids_of_way.resize(count);
  
    cerr<<'n';
    Way_Id_Way_Dump dump(env.offset, count, ll_idx_);
    select_all< Way_Id_Way_Dump >(dump);
    cerr<<'n';
    lseek64(source_fd, 0, SEEK_SET);
    
    uint32* tag_rd_buf = (uint32*) malloc(WAY_TAG_SORT_BUFFER_SIZE);
    
    uint32 rd_buf_pos(0), max_pos(0);
    while ((max_pos =
	    read(source_fd, &(tag_rd_buf[rd_buf_pos]), WAY_TAG_SORT_BUFFER_SIZE - rd_buf_pos*sizeof(uint32))))
    {
      max_pos += rd_buf_pos*sizeof(uint32);
      rd_buf_pos = 0;
  
      while ((rd_buf_pos + 1 < max_pos / sizeof(uint32)) &&
	      (rd_buf_pos + tag_rd_buf[rd_buf_pos+1] + 1 < max_pos / sizeof(uint32)))
      {
	if (blocklet_of_id[tag_rd_buf[rd_buf_pos]] != 0xff)
	{
	  for (uint32 i(0); i < tag_rd_buf[rd_buf_pos+1]; ++i)
	  {
	    if ((tag_rd_buf[rd_buf_pos+2+i] >= env.offset) &&
			(tag_rd_buf[rd_buf_pos+2+i] - env.offset < count))
	    {
	      if (env.ids_of_way[tag_rd_buf[rd_buf_pos+2+i] - env.offset].size() < 65535)
		env.ids_of_way[tag_rd_buf[rd_buf_pos+2+i] - env.offset].push_back(tag_rd_buf[rd_buf_pos]);
	      else
	      {
		cerr<<"Way "<<dec<<tag_rd_buf[rd_buf_pos+2+i]<<" has more than 2^16 tags.\n";
		exit(0);
	      }
	    }
	  }
	}
	rd_buf_pos += tag_rd_buf[rd_buf_pos+1] + 2;
      }
      cerr<<'t';
    
      memmove(tag_rd_buf, &(tag_rd_buf[rd_buf_pos]), WAY_TAG_SORT_BUFFER_SIZE - rd_buf_pos*sizeof(uint32));
      rd_buf_pos = WAY_TAG_SORT_BUFFER_SIZE / sizeof(uint32) - rd_buf_pos;
    }
    env.read_order.clear();
    for (uint32 i(0); i < count; ++i)
    {
      if (env.ids_of_way[i].size() > 0)
	env.read_order.push_back(i);
    }
    sort(env.read_order.begin(), env.read_order.end(), way_idx_less(ll_idx_));
    
    free(tag_rd_buf);
    
    flush_data< Tag_Way_Id_Writer >(env, env.begin(), env.end());
    
    env.offset += count;
  }
  
  make_block_index< Tag_Way_Id_Writer >(env);
    
  free(ll_idx_);
  free(blocklet_of_id);
  
  close(source_fd);
}

void way_tag_id_statistics()
{
  int dest_fd = open64((DATADIR + db_subdir + WAY_TAG_ID_STATS).c_str(),
                       O_WRONLY|O_CREAT|O_TRUNC,
		       S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  close(dest_fd);
  
  dest_fd = open64((DATADIR + db_subdir + WAY_TAG_ID_STATS).c_str(),
                   O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  if (dest_fd < 0)
    throw File_Error(errno, DATADIR + db_subdir + WAY_TAG_ID_STATS,
                     "way_tag_id_statistics:1");
  
  vector< uint32 > id_count(WayCollection::next_way_tag_id);
  Way_Tag_Id_Count_Local_Reader local_stats(id_count);
  select_all< Way_Tag_Id_Count_Local_Reader >(local_stats);
  Way_Tag_Id_Count_Global_Reader global_stats(id_count);
  select_all< Way_Tag_Id_Count_Global_Reader >(global_stats);
  
  for (vector< uint32 >::const_iterator it(id_count.begin()); it != id_count.end(); ++it)
    write(dest_fd, &(*it), sizeof(uint32));

  close(dest_fd);
}

//-----------------------------------------------------------------------------

struct Way_String_Cache
{
  static const vector< uint32 >& get_spatial_boundaries()
  {
    if (spatial_boundaries.empty())
      init();
    return spatial_boundaries;
  }
  
  static const vector< vector< pair< string, string > > >& get_kv_to_id_idx()
  {
    if (kv_to_id_idx.empty())
      init();
    return kv_to_id_idx;
  }
  
  static const vector< vector< uint16 > >& get_kv_to_id_block_idx()
  {
    if (kv_to_id_block_idx.empty())
      init();
    return kv_to_id_block_idx;
  }
  
  static uint32 get_next_way_tag_id()
  {
    if (next_way_tag_id == 0xffffffff)
      init();
    return next_way_tag_id;
  }
  
  static void set_next_way_tag_id(uint32 i)
  {
    next_way_tag_id = i;
  }
  
  static void reset()
  {
    spatial_boundaries.clear();
    kv_to_id_idx.clear();
    kv_to_id_block_idx.clear();
    next_way_tag_id = 0xffffffff;
  }
  
  private:
    static void init()
    {
      spatial_boundaries.clear();
    
      int string_idx_fd =
        open64((DATADIR + db_subdir + WAY_STRING_IDX).c_str(), O_RDONLY);
      if (string_idx_fd < 0)
	throw File_Error(errno, DATADIR + db_subdir + WAY_STRING_IDX,
	                 "Way_String_Cache.init():1");
  
      uint32* string_spat_idx_buf = (uint32*) malloc(WAY_TAG_SPATIAL_PARTS*sizeof(uint32));
      read(string_idx_fd, &next_way_tag_id, sizeof(uint32));
      read(string_idx_fd, string_spat_idx_buf, WAY_TAG_SPATIAL_PARTS*sizeof(uint32));
      for (uint32 i(0); i < WAY_TAG_SPATIAL_PARTS; ++i)
	spatial_boundaries.push_back(string_spat_idx_buf[i]);
      free(string_spat_idx_buf);
    
      vector< vector< pair< pair< string, string >, uint16 > > >
	  kv_to_all(WAY_TAG_SPATIAL_PARTS+1);
      
      uint16* kv_to_id_idx_buf_1 = (uint16*) malloc(3*sizeof(uint16));
      char* kv_to_id_idx_buf_2 = (char*) malloc(2*64*1024);
      uint32 block_id(0);
      while (read(string_idx_fd, kv_to_id_idx_buf_1, 3*sizeof(uint16)))
      {
	read(string_idx_fd, kv_to_id_idx_buf_2, kv_to_id_idx_buf_1[1] + kv_to_id_idx_buf_1[2]);
	kv_to_all[kv_to_id_idx_buf_1[0]].push_back(make_pair< pair< string, string >, uint16 >
	    (make_pair< string, string >
	     (string(kv_to_id_idx_buf_2, kv_to_id_idx_buf_1[1]),
	      string(&(kv_to_id_idx_buf_2[kv_to_id_idx_buf_1[1]]), kv_to_id_idx_buf_1[2])),
		       block_id++));
      }
      free(kv_to_id_idx_buf_2);
      free(kv_to_id_idx_buf_1);
  
      close(string_idx_fd);
    
      kv_to_id_idx.clear();
      kv_to_id_idx.resize(WAY_TAG_SPATIAL_PARTS+1);
      kv_to_id_block_idx.clear();
      kv_to_id_block_idx.resize(WAY_TAG_SPATIAL_PARTS+1);
      for (uint32 i(0); i < WAY_TAG_SPATIAL_PARTS+1; ++i)
      {
	sort(kv_to_all[i].begin(), kv_to_all[i].end());
	for (vector< pair< pair< string, string >, uint16 > >::const_iterator
		    it(kv_to_all[i].begin()); it != kv_to_all[i].end(); ++it)
	{
	  kv_to_id_idx[i].push_back(it->first);
	  kv_to_id_block_idx[i].push_back(it->second);
	}
      }
    }
  
    static vector< uint32 > spatial_boundaries;
    static vector< vector< pair< string, string > > > kv_to_id_idx;
    static vector< vector< uint16 > > kv_to_id_block_idx;
    static uint32 next_way_tag_id;
};

vector< uint32 > Way_String_Cache::spatial_boundaries;
vector< vector< pair< string, string > > > Way_String_Cache::kv_to_id_idx;
vector< vector< uint16 > > Way_String_Cache::kv_to_id_block_idx;
uint32 Way_String_Cache::next_way_tag_id;

//-----------------------------------------------------------------------------

inline bool is_local_here(map< pair< string, string >, pair< uint32, uint32 >* >::iterator elem_it2,
                          uint16 spatial_part, const vector< uint32 >& spatial_boundaries)
{
  if (spatial_part >= 2)
  {
    if ((elem_it2->second->first < spatial_boundaries[spatial_part-1])
        && (elem_it2->second->first >= spatial_boundaries[spatial_part-2]))
      return true;
  }
  else if (spatial_part == 1)
  {
    if (elem_it2->second->first < spatial_boundaries[0])
      return true;
  }
  return false;
}

inline int stringpair_cstringpair_compare(const pair< string, string > stringpair,
                                          uint8* cstringpairbuf)
{
  const uint16& key_len(*(uint16*)&(cstringpairbuf[0]));
  int key_cmp(strncmp(stringpair.first.c_str(), (char*)&(cstringpairbuf[4]), key_len));
  if (key_cmp)
    return key_cmp;
  if (stringpair.first.size() > key_len)
    return 1;
  const uint16& value_len(*(uint16*)&(cstringpairbuf[2]));
  int value_cmp(strncmp(stringpair.second.c_str(), (char*)&(cstringpairbuf[4 + key_len]), value_len));
  if (value_cmp)
    return value_cmp;
  if (stringpair.second.size() > value_len)
    return 1;
  return 0;
}

// constraints:
// all flush_data constraints
void way_string_delete_insert(map< pair< string, string >, pair< uint32, uint32 >* >& new_tags_ids,
                               set< pair< uint32, uint32 > >& moved_local_ids,
			       vector< uint32 >& local_id_idx, vector< uint32 >& spatial_boundaries_)
{
  if (new_tags_ids.empty())
    return;
  
  const uint32 BLOCKSIZE(WAY_STRING_BLOCK_SIZE);
  const vector< uint32 >& spatial_boundaries(Way_String_Cache::get_spatial_boundaries());
  spatial_boundaries_ = spatial_boundaries;
  const vector< vector< pair< string, string > > >& kv_to_id_idx(Way_String_Cache::get_kv_to_id_idx());
  const vector< vector< uint16 > >& kv_to_id_block_idx(Way_String_Cache::get_kv_to_id_block_idx());
  uint32 next_way_tag_id(Way_String_Cache::get_next_way_tag_id());
  vector< pair< string, string > > new_block_kvs;
  vector< uint16 > new_block_spatial;
  local_id_idx.resize(next_way_tag_id);
  
  uint block_id_bound(0);
  for (vector< vector< uint16 > >::const_iterator it(kv_to_id_block_idx.begin());
       it != kv_to_id_block_idx.end(); ++it)
    block_id_bound += it->size();
  int next_block_id(block_id_bound);
  vector< uint16 > spatial_part_in_block(block_id_bound);
  for (uint i(0); i < kv_to_id_block_idx.size(); ++i)
  {
    for (vector< uint16 >::const_iterator it(kv_to_id_block_idx[i].begin());
	 it != kv_to_id_block_idx[i].end(); ++it)
      spatial_part_in_block[*it] = i;
  }
  
  int dest_fd = open64((DATADIR + db_subdir + WAY_STRING_DATA).c_str(),
                       O_RDWR|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  if (dest_fd < 0)
    throw File_Error(errno, DATADIR + db_subdir + WAY_STRING_DATA,
                     "way_string_delete_insert:1");
  
  uint8* source_buf = (uint8*) malloc(BLOCKSIZE);
  uint8* deletion_buf = (uint8*) malloc(BLOCKSIZE);
  uint8* dest_buf = (uint8*) malloc(BLOCKSIZE);
  
  uint cur_source_block(0);
  while (cur_source_block < block_id_bound)
  {
    if (spatial_part_in_block[cur_source_block] != 0)
    {
      ++cur_source_block;
      continue;
    }
    
    map< pair< string, string >, pair< uint32, uint32 >* >::iterator elem_it;
    map< pair< string, string >, pair< uint32, uint32 >* >::iterator elem_end;
    if (kv_to_id_block_idx[spatial_part_in_block[cur_source_block]][0] == cur_source_block)
    {
      elem_it = new_tags_ids.begin();
      if (1 < kv_to_id_block_idx[spatial_part_in_block[cur_source_block]].size())
        elem_end = new_tags_ids.lower_bound(kv_to_id_idx[spatial_part_in_block[cur_source_block]][1]);
      else
        elem_end = new_tags_ids.end();
    }
    else
    {
      for (uint i(1); i < kv_to_id_block_idx[spatial_part_in_block[cur_source_block]].size(); ++i)
      {
        if (kv_to_id_block_idx[spatial_part_in_block[cur_source_block]][i] == cur_source_block)
        {
	  elem_it = new_tags_ids.lower_bound(kv_to_id_idx[spatial_part_in_block[cur_source_block]][i]);
          if (++i < kv_to_id_block_idx[spatial_part_in_block[cur_source_block]].size())
            elem_end = new_tags_ids.lower_bound(kv_to_id_idx[spatial_part_in_block[cur_source_block]][i]);
          else
            elem_end = new_tags_ids.end();
        }
      }
    }
    
    if (elem_it == elem_end)
    {
      ++cur_source_block;
      continue;
    }
    
    lseek64(dest_fd, (int64)cur_source_block*(BLOCKSIZE), SEEK_SET);
    read(dest_fd, source_buf, BLOCKSIZE);
    uint32 pos(sizeof(uint32));
    map< pair< string, string >, pair< uint32, uint32 >* >::iterator elem_it2(elem_it);
    while ((elem_it2 != elem_end) && (pos < *((uint32*)source_buf) + sizeof(uint32)))
    {
      int cmp_val(stringpair_cstringpair_compare(elem_it2->first, &(source_buf[pos+8])));
      uint32 size_of_buf(12 + *(uint16*)&(source_buf[pos+8]) + *(uint16*)&(source_buf[pos+10]));
      if (cmp_val < 0)
	++elem_it2;
      else if (cmp_val == 0)
      {
	elem_it2->second->first = 0xffffffff;
	elem_it2->second->second = *(uint32*)&(source_buf[pos]);
        pos += size_of_buf;
      }
      else
	pos += size_of_buf;
    }
    
    ++cur_source_block;
  }
    
  cur_source_block = 0;
  uint cur_dest_block(0);
  while (cur_source_block < block_id_bound)
  {
    if (spatial_part_in_block[cur_source_block] == 0)
    {
      ++cur_source_block;
      continue;
    }
    
    uint32 new_byte_count(0);
    map< pair< string, string >, pair< uint32, uint32 >* >::iterator elem_it;
    map< pair< string, string >, pair< uint32, uint32 >* >::iterator elem_end;
    uint16 spatial(spatial_part_in_block[cur_source_block]);
    if (kv_to_id_block_idx[spatial][0] == cur_source_block)
    {
      elem_it = new_tags_ids.begin();
      if (1 < kv_to_id_block_idx[spatial].size())
	elem_end = new_tags_ids.lower_bound(kv_to_id_idx[spatial][1]);
      else
	elem_end = new_tags_ids.end();
    }
    else
    {
      for (uint i(1); i < kv_to_id_block_idx[spatial].size(); ++i)
      {
	if (kv_to_id_block_idx[spatial][i] == cur_source_block)
	{
	  elem_it = new_tags_ids.lower_bound(kv_to_id_idx[spatial][i]);
	  if (i+1 < kv_to_id_block_idx[spatial].size())
	    elem_end = new_tags_ids.lower_bound
		(kv_to_id_idx[spatial][i+1]);
	  else
	    elem_end = new_tags_ids.end();
	}
      }
    }
    if (elem_it == elem_end)
    {
      ++cur_source_block;
      continue;
    }
    
    lseek64(dest_fd, (int64)cur_source_block*(BLOCKSIZE), SEEK_SET);
    read(dest_fd, source_buf, BLOCKSIZE);
    cur_dest_block = cur_source_block;
    uint32 pos(sizeof(uint32));
    uint32 elem_count(0);
    map< pair< string, string >, pair< uint32, uint32 >* >::iterator elem_it2(elem_it);
    while ((elem_it2 != elem_end) && (pos < *((uint32*)source_buf) + sizeof(uint32)))
    {
      int cmp_val(stringpair_cstringpair_compare(elem_it2->first, &(source_buf[pos+8])));
      uint32 size_of_buf(12 + *(uint16*)&(source_buf[pos+8]) + *(uint16*)&(source_buf[pos+10]));
      if (cmp_val < 0)
      {
	if (is_local_here(elem_it2, spatial_part_in_block[cur_source_block], spatial_boundaries))
	{
	  elem_it2->second->second = next_way_tag_id++;
	  new_byte_count += elem_it2->first.first.size() + elem_it2->first.second.size() + 12;
	}
	++elem_it2;
      }
      else if (cmp_val == 0)
      {
	local_id_idx[*(uint32*)&(source_buf[pos])] =
	    (*(uint32*)&(source_buf[pos+4]) | spatial_part_in_block[cur_source_block]);
	elem_it2->second->second = *(uint32*)&(source_buf[pos]);
	if (elem_it2->second->first == *(uint32*)&(source_buf[pos+4]))
	  new_byte_count += size_of_buf;
	else
	{
	  elem_it2->second->first = 0xffffffff;
	  moved_local_ids.insert(make_pair< uint32, uint32 >
	      (*(uint32*)&(source_buf[pos+4]), *(uint32*)&(source_buf[pos])));
	}
	deletion_buf[elem_count] = 0;
	pos += size_of_buf;
	++elem_count;
	++elem_it2;
      }
      else
      {
	local_id_idx[*(uint32*)&(source_buf[pos])] = 
	    (*(uint32*)&(source_buf[pos+4]) | spatial_part_in_block[cur_source_block]);
	deletion_buf[elem_count] = 1;
	new_byte_count += size_of_buf;
	pos += size_of_buf;
	++elem_count;
      }
    }
    
    while (pos < *((uint32*)source_buf) + sizeof(uint32))
    {
      uint32 size_of_buf(12 + *(uint16*)&(source_buf[pos+8]) + *(uint16*)&(source_buf[pos+10]));
      deletion_buf[elem_count] = 1;
      new_byte_count += size_of_buf;
      pos += size_of_buf;
      ++elem_count;
    }
    while (elem_it2 != elem_end)
    {
      if (is_local_here(elem_it2, spatial_part_in_block[cur_source_block], spatial_boundaries))
      {
	elem_it2->second->second = next_way_tag_id++;
	new_byte_count += elem_it2->first.first.size() + elem_it2->first.second.size() + 12;
      }
      ++elem_it2;
    }
    
    uint32 i(sizeof(uint32));
    elem_count = 0;
    while (new_byte_count > BLOCKSIZE - sizeof(uint32))
    {
      uint32 blocksize(new_byte_count/(new_byte_count/BLOCKSIZE + 1));
        
      uint32 j(sizeof(uint32));
      while ((j < blocksize) &&
	      (elem_it != elem_it2) && (i < ((uint32*)source_buf)[0]) &&
	      (j < BLOCKSIZE - (12 + elem_it->first.first.size() + elem_it->first.second.size())) &&
	      (j < BLOCKSIZE - (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]))))
      {
	int cmp_val(stringpair_cstringpair_compare(elem_it->first, &(source_buf[i+8])));
	if (cmp_val < 0)
	{
	  if (is_local_here(elem_it, spatial_part_in_block[cur_source_block], spatial_boundaries))
	  {
	    *(uint32*)&(dest_buf[j]) = elem_it->second->second;
	    *(uint32*)&(dest_buf[j+4]) = elem_it->second->first;
	    *(uint16*)&(dest_buf[j+8]) = elem_it->first.first.size();
	    *(uint16*)&(dest_buf[j+10]) = elem_it->first.second.size();
	    elem_it->first.first.copy((char*)&(dest_buf[j+12]), string::npos);
	    elem_it->first.second.copy((char*)&(dest_buf[j+12+elem_it->first.first.size()]), string::npos);
	    j += (12 + elem_it->first.first.size() + elem_it->first.second.size());
	  }
	  ++elem_it;
	}
	else
	{
	  if (deletion_buf[elem_count])
	  {
	    memcpy(&(dest_buf[j]), &(source_buf[i]),
		     (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10])));
	    j += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
	  }
	  ++elem_count;
	  i += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
	}
      }
      while ((j < blocksize) &&
	      (elem_it != elem_it2) &&
	      (j < BLOCKSIZE - (12 + elem_it->first.first.size() + elem_it->first.second.size())) &&
	      ((i >= ((uint32*)source_buf)[0]) ||
	      (stringpair_cstringpair_compare(elem_it->first, &(source_buf[i+8])) < 0)))
      {
	if (is_local_here(elem_it, spatial_part_in_block[cur_source_block], spatial_boundaries))
	{
	  *(uint32*)&(dest_buf[j]) = elem_it->second->second;
	  *(uint32*)&(dest_buf[j+4]) = elem_it->second->first;
	  *(uint16*)&(dest_buf[j+8]) = elem_it->first.first.size();
	  *(uint16*)&(dest_buf[j+10]) = elem_it->first.second.size();
	  elem_it->first.first.copy((char*)&(dest_buf[j+12]), string::npos);
	  elem_it->first.second.copy((char*)&(dest_buf[j+12+elem_it->first.first.size()]), string::npos);
	  j += (12 + elem_it->first.first.size() + elem_it->first.second.size());
	}
	++elem_it;
      }
      while ((j < blocksize) &&
	      (i < ((uint32*)source_buf)[0]) &&
	      (j < BLOCKSIZE - (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10])) &&
	      ((elem_it == elem_it2) ||
	      (stringpair_cstringpair_compare(elem_it->first, &(source_buf[i+8])) >= 0))))
      {
	if (deletion_buf[elem_count])
	{
	  memcpy(&(dest_buf[j]), &(source_buf[i]),
		   (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10])));
	  j += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
	}
	++elem_count;
	i += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
      }

      lseek64(dest_fd, (int64)cur_dest_block*(BLOCKSIZE), SEEK_SET);
      ((uint32*)dest_buf)[0] = j - sizeof(uint32);
      write(dest_fd, dest_buf, BLOCKSIZE);

      cur_dest_block = next_block_id;
      if ((i >= ((uint32*)source_buf)[0]) ||
	   (stringpair_cstringpair_compare(elem_it->first, &(source_buf[i+8])) < 0))
	new_block_kvs.push_back(elem_it->first);
      else
      {
	string key((char*)&(dest_buf[j+12]), *(uint16*)&(dest_buf[j+8]));
	string value((char*)&(dest_buf[j+12+*(uint16*)&(dest_buf[j+8])]), *(uint16*)&(dest_buf[j+10]));
	new_block_kvs.push_back(make_pair< string, string >
	    (key, value));
      }
      new_block_spatial.push_back(spatial_part_in_block[cur_source_block]);
      ++next_block_id;
      new_byte_count -= (j - sizeof(uint32));
    }
    
    uint32 j(sizeof(uint32));
    while ((elem_it != elem_it2) && (i < ((uint32*)source_buf)[0]))
    {
      int cmp_val(stringpair_cstringpair_compare(elem_it->first, &(source_buf[i+8])));
      
      if (cmp_val < 0)
      {
	if (is_local_here(elem_it, spatial_part_in_block[cur_source_block], spatial_boundaries))
	{
	  *(uint32*)&(dest_buf[j]) = elem_it->second->second;
	  *(uint32*)&(dest_buf[j+4]) = elem_it->second->first;
	  *(uint16*)&(dest_buf[j+8]) = elem_it->first.first.size();
	  *(uint16*)&(dest_buf[j+10]) = elem_it->first.second.size();
	  elem_it->first.first.copy((char*)&(dest_buf[j+12]), string::npos);
	  elem_it->first.second.copy((char*)&(dest_buf[j+12+elem_it->first.first.size()]), string::npos);
	  j += (12 + elem_it->first.first.size() + elem_it->first.second.size());
	}
	++elem_it;
      }
      else
      {
	if (deletion_buf[elem_count])
	{
	  memcpy(&(dest_buf[j]), &(source_buf[i]),
		   (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10])));
	  j += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
	}
	++elem_count;
	i += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
      }
    }
    
    while (elem_it != elem_it2)
    {
      if (is_local_here(elem_it, spatial_part_in_block[cur_source_block], spatial_boundaries))
      {
	*(uint32*)&(dest_buf[j]) = elem_it->second->second;
	*(uint32*)&(dest_buf[j+4]) = elem_it->second->first;
	*(uint16*)&(dest_buf[j+8]) = elem_it->first.first.size();
	*(uint16*)&(dest_buf[j+10]) = elem_it->first.second.size();
	elem_it->first.first.copy((char*)&(dest_buf[j+12]), string::npos);
	elem_it->first.second.copy((char*)&(dest_buf[j+12+elem_it->first.first.size()]), string::npos);
	j += (12 + elem_it->first.first.size() + elem_it->first.second.size());
      }
      ++elem_it;
    }
    while (i < ((uint32*)source_buf)[0])
    {
      if (deletion_buf[elem_count])
      {
	memcpy(&(dest_buf[j]), &(source_buf[i]),
		 (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10])));
	j += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
      }
      ++elem_count;
      i += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
    }
    lseek64(dest_fd, (int64)cur_dest_block*(BLOCKSIZE), SEEK_SET);
    ((uint32*)dest_buf)[0] = j - sizeof(uint32);
    write(dest_fd, dest_buf, BLOCKSIZE);
    
    ++cur_source_block;
  }
    
  cur_source_block = 0;
  while (cur_source_block < block_id_bound)
  {
    if (spatial_part_in_block[cur_source_block] != 0)
    {
      ++cur_source_block;
      continue;
    }
    
    uint32 new_byte_count(0);
    map< pair< string, string >, pair< uint32, uint32 >* >::iterator elem_it;
    map< pair< string, string >, pair< uint32, uint32 >* >::iterator elem_end;
    if (kv_to_id_block_idx[spatial_part_in_block[cur_source_block]][0] == cur_source_block)
    {
      elem_it = new_tags_ids.begin();
      if (1 < kv_to_id_block_idx[spatial_part_in_block[cur_source_block]].size())
	elem_end = new_tags_ids.lower_bound(kv_to_id_idx[spatial_part_in_block[cur_source_block]][1]);
      else
	elem_end = new_tags_ids.end();
    }
    else
    {
      for (uint i(1); i < kv_to_id_block_idx[spatial_part_in_block[cur_source_block]].size(); ++i)
      {
	if (kv_to_id_block_idx[spatial_part_in_block[cur_source_block]][i] == cur_source_block)
	{
	  elem_it = new_tags_ids.lower_bound(kv_to_id_idx[spatial_part_in_block[cur_source_block]][i]);
	  if (++i < kv_to_id_block_idx[spatial_part_in_block[cur_source_block]].size())
	    elem_end = new_tags_ids.lower_bound(kv_to_id_idx[spatial_part_in_block[cur_source_block]][i]);
	  else
	    elem_end = new_tags_ids.end();
	}
      }
    }
    
    if (elem_it == elem_end)
    {
      ++cur_source_block;
      continue;
    }
    
    lseek64(dest_fd, (int64)cur_source_block*(BLOCKSIZE), SEEK_SET);
    read(dest_fd, source_buf, BLOCKSIZE);
    cur_dest_block = cur_source_block;
    uint32 pos(sizeof(uint32));
    uint32 elem_count(0);
    map< pair< string, string >, pair< uint32, uint32 >* >::iterator elem_it2(elem_it);
    while ((elem_it2 != elem_end) && (pos < *((uint32*)source_buf) + sizeof(uint32)))
    {
      int cmp_val(stringpair_cstringpair_compare(elem_it2->first, &(source_buf[pos+8])));
      uint32 size_of_buf(12 + *(uint16*)&(source_buf[pos+8]) + *(uint16*)&(source_buf[pos+10]));
      if (cmp_val < 0)
      {
	if (elem_it2->second->first == 0xffffffff)
	{
	  new_byte_count += elem_it2->first.first.size() + elem_it2->first.second.size() + 12;
	  if (elem_it2->second->second == 0xffffffff)
	    elem_it2->second->second = next_way_tag_id++;
	}
	++elem_it2;
      }
      else if (cmp_val == 0)
      {
	elem_it2->second->second = *(uint32*)&(source_buf[pos]);
	new_byte_count += size_of_buf;
	deletion_buf[elem_count] = 0;
	pos += size_of_buf;
	++elem_count;
	++elem_it2;
      }
      else
      {
	deletion_buf[elem_count] = 1;
	new_byte_count += size_of_buf;
	pos += size_of_buf;
	++elem_count;
      }
    }
    while (pos < *((uint32*)source_buf) + sizeof(uint32))
    {
      uint32 size_of_buf(12 + *(uint16*)&(source_buf[pos+8]) + *(uint16*)&(source_buf[pos+10]));
      deletion_buf[elem_count] = 1;
      new_byte_count += size_of_buf;
      pos += size_of_buf;
      ++elem_count;
    }
    while (elem_it2 != elem_end)
    {
      if (elem_it2->second->first == 0xffffffff)
      {
	new_byte_count += elem_it2->first.first.size() + elem_it2->first.second.size() + 12;
	if (elem_it2->second->second == 0xffffffff)
	  elem_it2->second->second = next_way_tag_id++;
      }
      ++elem_it2;
    }
    
    uint32 i(sizeof(uint32));
    elem_count = 0;
    while (new_byte_count > BLOCKSIZE - sizeof(uint32))
    {
      uint32 blocksize(new_byte_count/(new_byte_count/BLOCKSIZE + 1));
        
      uint32 j(sizeof(uint32));
      while ((j < blocksize) &&
	      (elem_it != elem_it2) && (i < ((uint32*)source_buf)[0]) &&
	      (j < BLOCKSIZE - (12 + elem_it->first.first.size() + elem_it->first.second.size())) &&
	      (j < BLOCKSIZE - (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]))))
      {
	int cmp_val(stringpair_cstringpair_compare(elem_it->first, &(source_buf[i+8])));
	if (cmp_val < 0)
	{
	  if (elem_it->second->first == 0xffffffff)
	  {
	    *(uint32*)&(dest_buf[j]) = elem_it->second->second;
	    *(uint32*)&(dest_buf[j+4]) = elem_it->second->first;
	    *(uint16*)&(dest_buf[j+8]) = elem_it->first.first.size();
	    *(uint16*)&(dest_buf[j+10]) = elem_it->first.second.size();
	    elem_it->first.first.copy((char*)&(dest_buf[j+12]), string::npos);
	    elem_it->first.second.copy((char*)&(dest_buf[j+12+elem_it->first.first.size()]), string::npos);
	    j += (12 + elem_it->first.first.size() + elem_it->first.second.size());
	  }
	  ++elem_it;
	}
	else
	{
	  if (deletion_buf[elem_count])
	  {
	    memcpy(&(dest_buf[j]), &(source_buf[i]),
		     (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10])));
	    j += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
	  }
	  ++elem_count;
	  i += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
	}
      }
      while ((j < blocksize) &&
	      (elem_it != elem_it2) &&
	      (j < BLOCKSIZE - (12 + elem_it->first.first.size() + elem_it->first.second.size())) &&
	      ((i >= ((uint32*)source_buf)[0]) ||
	      (stringpair_cstringpair_compare(elem_it->first, &(source_buf[i+8])) < 0)))
      {
	if (elem_it->second->first == 0xffffffff)
	{
	  *(uint32*)&(dest_buf[j]) = elem_it->second->second;
	  *(uint32*)&(dest_buf[j+4]) = elem_it->second->first;
	  *(uint16*)&(dest_buf[j+8]) = elem_it->first.first.size();
	  *(uint16*)&(dest_buf[j+10]) = elem_it->first.second.size();
	  elem_it->first.first.copy((char*)&(dest_buf[j+12]), string::npos);
	  elem_it->first.second.copy((char*)&(dest_buf[j+12+elem_it->first.first.size()]), string::npos);
	  j += (12 + elem_it->first.first.size() + elem_it->first.second.size());
	}
	++elem_it;
      }
      while ((j < blocksize) &&
	      (i < ((uint32*)source_buf)[0]) &&
	      (j < BLOCKSIZE - (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10])) &&
	      ((elem_it == elem_it2) ||
	      (stringpair_cstringpair_compare(elem_it->first, &(source_buf[i+8])) >= 0))))
      {
	if (deletion_buf[elem_count])
	{
	  memcpy(&(dest_buf[j]), &(source_buf[i]),
		   (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10])));
	  j += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
	}
	++elem_count;
	i += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
      }

      lseek64(dest_fd, (int64)cur_dest_block*(BLOCKSIZE), SEEK_SET);
      ((uint32*)dest_buf)[0] = j - sizeof(uint32);
      write(dest_fd, dest_buf, BLOCKSIZE);

      cur_dest_block = next_block_id;
      if ((i >= ((uint32*)source_buf)[0]) ||
	   (stringpair_cstringpair_compare(elem_it->first, &(source_buf[i+8])) < 0))
	new_block_kvs.push_back(elem_it->first);
      else
      {
	string key((char*)&(dest_buf[j+12]), *(uint16*)&(dest_buf[j+8]));
	string value((char*)&(dest_buf[j+12+*(uint16*)&(dest_buf[j+8])]), *(uint16*)&(dest_buf[j+10]));
	new_block_kvs.push_back(make_pair< string, string >
	    (key, value));
      }
      new_block_spatial.push_back(spatial_part_in_block[cur_source_block]);
      ++next_block_id;
      new_byte_count -= (j - sizeof(uint32));
    }
    
    uint32 j(sizeof(uint32));
    while ((elem_it != elem_it2) && (i < ((uint32*)source_buf)[0]))
    {
      int cmp_val(stringpair_cstringpair_compare(elem_it->first, &(source_buf[i+8])));
      if (cmp_val < 0)
      {
	if (elem_it->second->first == 0xffffffff)
	{
	  *(uint32*)&(dest_buf[j]) = elem_it->second->second;
	  *(uint32*)&(dest_buf[j+4]) = elem_it->second->first;
	  *(uint16*)&(dest_buf[j+8]) = elem_it->first.first.size();
	  *(uint16*)&(dest_buf[j+10]) = elem_it->first.second.size();
	  elem_it->first.first.copy((char*)&(dest_buf[j+12]), string::npos);
	  elem_it->first.second.copy((char*)&(dest_buf[j+12+elem_it->first.first.size()]), string::npos);
	  j += (12 + elem_it->first.first.size() + elem_it->first.second.size());
	}
	++elem_it;
      }
      else
      {
	if (deletion_buf[elem_count])
	{
	  memcpy(&(dest_buf[j]), &(source_buf[i]),
		   (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10])));
	  j += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
	}
	++elem_count;
	i += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
      }
    }
    while (elem_it != elem_it2)
    {
      if (elem_it->second->first == 0xffffffff)
      {
	*(uint32*)&(dest_buf[j]) = elem_it->second->second;
	*(uint32*)&(dest_buf[j+4]) = elem_it->second->first;
	*(uint16*)&(dest_buf[j+8]) = elem_it->first.first.size();
	*(uint16*)&(dest_buf[j+10]) = elem_it->first.second.size();
	elem_it->first.first.copy((char*)&(dest_buf[j+12]), string::npos);
	elem_it->first.second.copy((char*)&(dest_buf[j+12+elem_it->first.first.size()]), string::npos);
	j += (12 + elem_it->first.first.size() + elem_it->first.second.size());
      }
      ++elem_it;
    }
    while (i < ((uint32*)source_buf)[0])
    {
      if (deletion_buf[elem_count])
      {
	memcpy(&(dest_buf[j]), &(source_buf[i]),
		 (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10])));
	j += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
      }
      ++elem_count;
      i += (12 + *(uint16*)&(source_buf[i+8]) + *(uint16*)&(source_buf[i+10]));
    }
    
    lseek64(dest_fd, (int64)cur_dest_block*(BLOCKSIZE), SEEK_SET);
    ((uint32*)dest_buf)[0] = j - sizeof(uint32);
    write(dest_fd, dest_buf, BLOCKSIZE);
    
    ++cur_source_block;
  }
  
  free(source_buf);
  free(deletion_buf);
  free(dest_buf);

  close(dest_fd);
  
  //update Way_String_Cache
  Way_String_Cache::reset();
  Way_String_Cache::set_next_way_tag_id(next_way_tag_id);
  
  int string_idx_fd = open64((DATADIR + db_subdir + WAY_STRING_IDX).c_str(), O_WRONLY|O_APPEND);
  if (string_idx_fd < 0)
    throw File_Error(errno, DATADIR + db_subdir + WAY_STRING_IDX, "Way_String_Cache.init():1");
  
  for (unsigned int i(0); i < new_block_spatial.size(); ++i)
  {
    uint16 key_len(new_block_kvs[i].first.size()), value_len(new_block_kvs[i].second.size());
    write(string_idx_fd, &(new_block_spatial[i]), 2);
    write(string_idx_fd, &key_len, 2);
    write(string_idx_fd, &value_len, 2);
    write(string_idx_fd, new_block_kvs[i].first.data(), key_len);
    write(string_idx_fd, new_block_kvs[i].second.data(), value_len);
  }
  
  close(string_idx_fd);

  string_idx_fd = open64((DATADIR + db_subdir + WAY_STRING_IDX).c_str(), O_WRONLY);
  if (string_idx_fd < 0)
    throw File_Error(errno, DATADIR + db_subdir + WAY_STRING_IDX, "Way_String_Cache.init():1");
  
  write(string_idx_fd, &next_way_tag_id, sizeof(uint32));
  
  close(string_idx_fd);
}

//-----------------------------------------------------------------------------

void way_tag_id_statistics_remake()
{
  int dest_fd = open64((DATADIR + db_subdir + WAY_TAG_ID_STATS).c_str(),
                       O_WRONLY|O_CREAT|O_TRUNC,
                       S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  close(dest_fd);
  
  dest_fd = open64((DATADIR + db_subdir + WAY_TAG_ID_STATS).c_str(),
                   O_WRONLY|O_CREAT, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  if (dest_fd < 0)
    throw File_Error(errno, DATADIR + db_subdir + WAY_TAG_ID_STATS,
                     "way_tag_id_statistics_remake:1");
  
  vector< uint32 > id_count(Way_String_Cache::get_next_way_tag_id());
  Way_Tag_Id_Count_Local_Reader local_stats(id_count);
  select_all< Way_Tag_Id_Count_Local_Reader >(local_stats);
  Way_Tag_Id_Count_Global_Reader global_stats(id_count);
  select_all< Way_Tag_Id_Count_Global_Reader >(global_stats);

  for (vector< uint32 >::const_iterator it(id_count.begin()); it != id_count.end(); ++it)
    write(dest_fd, &(*it), sizeof(uint32));
  
  close(dest_fd);
}

//-----------------------------------------------------------------------------

void select_way_kv_to_ids
    (string key, string value, set< uint32 >& string_ids_global,
     set< uint32 >& string_ids_local, set< uint32 >& string_idxs_local)
{
  const vector< vector< pair< string, string > > >& kv_to_id_idx
      (Way_String_Cache::get_kv_to_id_idx());
  const vector< vector< uint16 > >& kv_to_id_block_idx
      (Way_String_Cache::get_kv_to_id_block_idx());
  
  set< uint16 > kv_to_idx_block_ids;
  for (uint32 i(0); i < WAY_TAG_SPATIAL_PARTS+1; ++i)
  {
    uint32 j(1);
    if (value == "")
    {
      while ((j < kv_to_id_idx[i].size()) && (kv_to_id_idx[i][j].first < key))
	++j;
      kv_to_idx_block_ids.insert(kv_to_id_block_idx[i][j-1]);
      while ((j < kv_to_id_idx[i].size()) && (kv_to_id_idx[i][j].first <= key))
      {
	kv_to_idx_block_ids.insert(kv_to_id_block_idx[i][j]);
	++j;
      }
    }
    else
    {
      while ((j < kv_to_id_idx[i].size()) && ((kv_to_id_idx[i][j].first < key) ||
	      ((kv_to_id_idx[i][j].first == key) && (kv_to_id_idx[i][j].second < value))))
	++j;
      kv_to_idx_block_ids.insert(kv_to_id_block_idx[i][j-1]);
      while ((j < kv_to_id_idx[i].size()) && ((kv_to_id_idx[i][j].first < key) ||
	      ((kv_to_id_idx[i][j].first == key) && (kv_to_id_idx[i][j].second <= value))))
      {
	kv_to_idx_block_ids.insert(kv_to_id_block_idx[i][j]);
	++j;
      }
    }
  }

  int string_fd = open64((DATADIR + db_subdir + WAY_STRING_DATA).c_str(),
                         O_RDONLY);
  if (string_fd < 0)
    throw File_Error(errno, DATADIR + db_subdir + WAY_STRING_DATA,
                     "select_kv_to_ids:1");
  
  char* string_idxs_buf = (char*) malloc(WAY_STRING_BLOCK_SIZE);
  if (value == "")
  {
    for (set< uint16 >::const_iterator it(kv_to_idx_block_ids.begin());
	 it != kv_to_idx_block_ids.end(); ++it)
    {
      lseek64(string_fd, ((uint64)(*it))*WAY_STRING_BLOCK_SIZE, SEEK_SET);
      read(string_fd, string_idxs_buf, WAY_STRING_BLOCK_SIZE);
      uint32 pos(sizeof(uint32));
      while (pos < *((uint32*)string_idxs_buf) + sizeof(uint32))
      {
	pos += 2*sizeof(uint32);
	uint16& key_len(*((uint16*)&(string_idxs_buf[pos])));
	pos += sizeof(uint16);
	uint16& value_len(*((uint16*)&(string_idxs_buf[pos])));
	pos += sizeof(uint16);
	if ((key_len >= key.size()) && (!(strncmp(key.c_str(), &(string_idxs_buf[pos]), key_len))))
	{
	  if (*((uint32*)&(string_idxs_buf[pos - 2*sizeof(uint16) - sizeof(uint32)]))
			== 0xffffffff)
	    string_ids_global.insert
		(*((uint32*)&(string_idxs_buf[pos - 2*sizeof(uint16) - 2*sizeof(uint32)])));
	  else
	  {
	    string_idxs_local.insert
		(*((uint32*)&(string_idxs_buf[pos - 2*sizeof(uint16) - sizeof(uint32)])));
	    string_ids_local.insert
		(*((uint32*)&(string_idxs_buf[pos - 2*sizeof(uint16) - 2*sizeof(uint32)])));
	  }
	}
	pos += key_len + value_len;
      }
    }
  }
  else
  {
    for (set< uint16 >::const_iterator it(kv_to_idx_block_ids.begin());
	 it != kv_to_idx_block_ids.end(); ++it)
    {
      lseek64(string_fd, ((uint64)(*it))*WAY_STRING_BLOCK_SIZE, SEEK_SET);
      read(string_fd, string_idxs_buf, WAY_STRING_BLOCK_SIZE);
      uint32 pos(sizeof(uint32));
      while (pos < *((uint32*)string_idxs_buf) + sizeof(uint32))
      {
	pos += 2*sizeof(uint32);
	uint16& key_len(*((uint16*)&(string_idxs_buf[pos])));
	pos += sizeof(uint16);
	uint16& value_len(*((uint16*)&(string_idxs_buf[pos])));
	pos += sizeof(uint16);
	if ((key_len >= key.size()) && (value_len >= value.size()) &&
		    (!(strncmp(key.c_str(), &(string_idxs_buf[pos]), key_len))) &&
		    (!(strncmp(value.c_str(), &(string_idxs_buf[pos + key_len]), value_len))))
	{
	  if (*((uint32*)&(string_idxs_buf[pos - 2*sizeof(uint16) - sizeof(uint32)]))
			== 0xffffffff)
	    string_ids_global.insert
		(*((uint32*)&(string_idxs_buf[pos - 2*sizeof(uint16) - 2*sizeof(uint32)])));
	  else
	  {
	    string_idxs_local.insert
		(*((uint32*)&(string_idxs_buf[pos - 2*sizeof(uint16) - sizeof(uint32)])));
	    string_ids_local.insert
		(*((uint32*)&(string_idxs_buf[pos - 2*sizeof(uint16) - 2*sizeof(uint32)])));
	  }
	}
	pos += key_len + value_len;
      }
    }
  }
  free(string_idxs_buf);
  
  close(string_fd);
}

void select_way_ids_to_kvs
    (const map< uint32, uint32 > ids_local,
     const set< uint32 > ids_global,
     map< uint32, pair< string, string > >& kvs)
{
  const vector< uint32 >& spatial_boundaries(Way_String_Cache::get_spatial_boundaries());
  const vector< vector< uint16 > >& kv_to_id_block_idx(Way_String_Cache::get_kv_to_id_block_idx());
  
  vector< bool > used_spat_parts(WAY_TAG_SPATIAL_PARTS);
  for (map< uint32, uint32 >::const_iterator it(ids_local.begin());
       it != ids_local.end(); ++it)
  {
    uint32 i(0);
    while (spatial_boundaries[i] <= it->second)
      ++i;
    used_spat_parts[i] = true;
  }
  set< uint16 > used_blocks;
  for (vector< uint16 >::const_iterator it(kv_to_id_block_idx[0].begin());
       it != kv_to_id_block_idx[0].end(); ++it)
    used_blocks.insert(*it);
  for (uint32 i(1); i < used_spat_parts.size()+1; ++i)
  {
    if (used_spat_parts[i-1])
    {
      for (vector< uint16 >::const_iterator it(kv_to_id_block_idx[i].begin());
	   it != kv_to_id_block_idx[i].end(); ++it)
	used_blocks.insert(*it);
    }
  }

  int string_fd = open64((DATADIR + db_subdir + WAY_STRING_DATA).c_str(),
                         O_RDONLY);
  if (string_fd < 0)
    throw File_Error(errno, DATADIR + db_subdir + WAY_STRING_DATA,
                     "select_ids_to_kvs:1");
  
  char* string_idxs_buf = (char*) malloc(WAY_STRING_BLOCK_SIZE);
  for (set< uint16 >::const_iterator it(used_blocks.begin());
       it != used_blocks.end(); ++it)
  {
    lseek64(string_fd, ((uint64)(*it))*WAY_STRING_BLOCK_SIZE, SEEK_SET);
    read(string_fd, string_idxs_buf, WAY_STRING_BLOCK_SIZE);
    uint32 pos(sizeof(uint32));
    while (pos < *((uint32*)string_idxs_buf) + sizeof(uint32))
    {
      uint16& key_len(*((uint16*)&(string_idxs_buf[pos + 2*sizeof(uint32)])));
      uint16& value_len(*((uint16*)&(string_idxs_buf[pos + 2*sizeof(uint32) + sizeof(uint16)])));
      if ((ids_global.find(*((uint32*)&(string_idxs_buf[pos]))) != ids_global.end()) ||
	   (ids_local.find(*((uint32*)&(string_idxs_buf[pos]))) != ids_local.end()))
	kvs[*((uint32*)&(string_idxs_buf[pos]))] = make_pair< string, string >
	    (string(&(string_idxs_buf[pos + 2*sizeof(uint32) + 2*sizeof(uint16)]), key_len),
	     string(&(string_idxs_buf[pos + 2*sizeof(uint32) + 2*sizeof(uint16) + key_len]), value_len));
	    pos += 2*sizeof(uint32) + 2*sizeof(uint16) + key_len + value_len;
    }
  }
  free(string_idxs_buf);
  
  close(string_fd);
}