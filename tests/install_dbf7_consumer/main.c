#include <sdb/c_api.h>
#include <stdio.h>
int main(int argc,char **argv) {
    sdb_reader *reader=0;
    uint64_t frames=0;
    double times[6],values[6];
    size_t count=0;
    if(argc!=2 || sdb_abi_version()!=1)return 1;
    if(sdb_reader_open(argv[1],&reader))return 2;
    if(sdb_reader_frame_count(reader,&frames) || frames!=4096)return 3;
    if(sdb_reader_history_f64(reader,"U",1,0,4090,4095,times,values,6,&count)) {
        fprintf(stderr,"%s\n",sdb_last_error());sdb_reader_close(reader);return 4;
    }
    sdb_reader_close(reader);
    return count==6 && values[0]==4090 && values[5]==4095?0:5;
}
