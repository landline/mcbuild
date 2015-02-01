#if 0
// Various options
struct {
    int autokill;
    
    int grinding;
    int maxlevel;
    
    int holeradar;
    
    int build;

} opt;

// used for detecting player position changes
int hr_last_x;
int hr_last_y;
int hr_last_z;
int hr_last_yaw;

////////////////////////////////////////////////////////////////////////////////
// helpers

uint64_t gettimestamp() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t ts = (uint64_t)tv.tv_sec*1000000+(uint64_t)tv.tv_usec;
}

#define MIN(a,b) ((a<b)?(a):(b))
#define MAX(a,b) ((a>b)?(a):(b))
#define SGN(x) (((x)>=0)?1:-1)
#define SQ(x) ((x)*(x))
static inline int sqdist(int x, int y, int z, int x2, int y2, int z2) {
    return SQ(x-x2)+SQ(y-y2)+SQ(z-z2);
}

void chat_message(const char *str, lh_buf_t *buf, const char *color) {
    uint8_t jreply[32768];
    ssize_t jlen = sprintf(jreply,
                           "{"
                           "\"text\":\"[MCP] %s\","
                           "\"color\":\"%s\""
                           "}",str,color?color:"red");
    uint8_t wbuf[65536];
    uint8_t *wp = wbuf;
    write_varint(wp, 0x02);
    write_varint(wp, jlen);
    memcpy(wp,jreply,jlen);
    write_packet(wbuf, (wp-wbuf)+jlen, buf);
}

int cprintf(lh_buf_t *buf, const char *color, const char *fmt, ...) {
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Hole Radar

#define HR_DISTC 2
#define HR_DISTB (HR_DISTC<<4)

void hole_radar(lh_buf_t *client) {
    int x = gs.own.x>>5;
    int y = (gs.own.y>>5)-1;
    int z = gs.own.z>>5;

    int lx=-(int)(65536*sin(gs.own.yaw/180*M_PI));
    int lz=(int)(65536*cos(gs.own.yaw/180*M_PI));

    int X=x>>4;
    int Z=z>>4;

    uint8_t * data;
    int off,sh;
    if (abs(lx) > abs(lz)) {
        lz=0;
        // looking into east or west direction
        if (lx<0) {
            //to west
            lx=-1;
            data = export_cuboid(X-HR_DISTC,X,Z,Z,y,y);
            off = (x&0x0f)+HR_DISTB+(z&0x0f)*(HR_DISTB+16);
        }
        else {
            // east
            lx=1;
            data = export_cuboid(X,X+HR_DISTC,Z,Z,y,y);
            off = (x&0x0f)+(z&0x0f)*(HR_DISTB+16);
        }
        sh=lx;
    }
    else {
        lx=0;
        // looking into north or south direction
        if (lz<0) {
            //to north
            lz=-1;
            data = export_cuboid(X,X,Z-HR_DISTC,Z,y,y);
            off = (x&0x0f)+((z&0x0f)+HR_DISTB)*16;
        }
        else {
            // south
            lz=1;
            data = export_cuboid(X,X,Z,Z+HR_DISTC,y,y);
            off = (x&0x0f)+(z&0x0f)*16;
        }
        sh = 16*lz;
    }

    int i;
    for(i=1; i<=HR_DISTB; i++) {
        off+=sh;
        if (data[off]==0) {
            char reply[32768];
            sprintf(reply, "*** HOLE *** : %d,%d d=%d",x+lx*i,z+lz*i,i);
            chat_message(reply, client, NULL);
            break;
        }
    }

    free(data);    
}

////////////////////////////////////////////////////////////////////////////////
// Autokill

#define MAX_ENTITIES     4096
#define MAX_ATTACK       1
#define MIN_ENTITY_DELAY 250000  // minimum interval between hitting the same entity (us)
#define MIN_ATTACK_DELAY  50000  // minimum interval between attacking any entity

int is_hostile_entity(entity *e) {
    return e->hostile > 0;
}

void autokill(lh_buf_t *server) {
    uint64_t ts = gettimestamp();
    if ((ts-gs.last_attack)<MIN_ATTACK_DELAY) return;

    // calculate list of hostile entities in range
    int hent[MAX_ENTITIES];

    //TODO: sort entities by how dangerous and how close they are
    int nhent = get_entities_in_range(hent,MAX_ENTITIES,5.0,is_hostile_entity,NULL);

    //if (nhent > 0) printf("%s : got %d entities to kill\n",__func__,nhent);
    
    //TODO: select primary weapon for priority targets

    //TODO: turn to target?

    int i,h;
    for(i=0,h=0; h<MAX_ATTACK && i<nhent; i++) {
        entity *e = gs.P(entity)+hent[i];
        if ((ts-e->lasthit) < MIN_ATTACK_DELAY)
            continue;
        
        uint8_t pkt[4096], *p;
        
        //printf("%lld : Attack entity %d\n", ts, e->id);
        
        // attack entity
        p = pkt;
        write_varint(p,0x02); // Use Entity
        write_int(p, e->id);
        write_char(p, 0x01);
        write_packet(pkt, p-pkt, server);
        
        // wave arm
        p = pkt;
        write_varint(p,0x0a); // Animation
        write_int(p,gs.own.id);
        write_char(p,0x01);
        write_packet(pkt, p-pkt, server);
        
        e->lasthit = ts;
        gs.last_attack = ts;
        h++;
    }
}

////////////////////////////////////////////////////////////////////////////////
// Inventory

char * get_item_name(char *buf, int id, int dmg) {
    if (id == 0xffff) {
        sprintf(buf, "-");
        return buf;
    }

    if (ITEMS[id].name) {
        int pos = sprintf(buf, "%s", ITEMS[id].name);
        if ((ITEMS[id].flags&I_MTYPE) && ITEMS[id].mname[dmg])
            sprintf(buf+pos, " (%s)",ITEMS[id].mname[dmg]);
        return buf;
    }
    return NULL;
}

void print_inventory() {
    int i,j;
    printf("---------------------------\n");
    printf("Inventory dump:\n");
    for(i=0; i<45; i++) {
        slot_t * s = &gs.inventory.slots[i];
        char buf[4096];

        if (get_item_name(buf, s->id, s->damage))
            printf(" %2d : %-20s x%-2d\n", i, buf, s->count);
        else
            printf(" %2d : %4x x%-2d dmg=%d %s\n",
                   i,s->id,s->count,s->damage,(s->dlen!=0xffff && s->dlen!=0)?"+data":"");
    }
    printf("---------------------------\n");
}

////////////////////////////////////////////////////////////////////////////////
// Autobuild

#define MIN_BUILD_DELAY  100000     // minimum delay between building two blocks
#define MIN_BLOCK_DELAY  1000000    // minimum delay between retrying to place the same block again
#define REACH_RANGE 5

#if 0
typedef struct {
    int x,y,z;          // block coordinates
    int v;              // type of placement, only applicable to slabs: 'u'=upper, 'd'=bottom, 'm'=default
    int id;             // block type ID
    uint8_t dir;        // 
    uint8_t cx,cy,cz;   // cursor x,y,z
} _bblock;

struct {
    int active;                 // building is in progress
    uint64_t last_placement;    // timestamp when the most recent block was placed
    lh_arr_declare(bblock,all);     // all blocks to be built - init by chat command
    lh_arr_declare(bblock,inreach); // all blocks in reach - updated by player position change
    int last_x, last_y, last_z;
} build;

#endif

#define DIR_UP      0
#define DIR_DOWN    1
#define DIR_SOUTH   2
#define DIR_NORTH   3
#define DIR_EAST    4
#define DIR_WEST    5

typedef struct {
    uint64_t ts;        // last timestamp we attempted to place this block
    int x,y,z;          // block coordinates
    int bid;            // block ID to place
    int meta;           // meta-value for the subtype
    uint32_t place[6];  // block placement methods in 6 available directions
                        // bits 31..28 : enabled
                        // bits 27..24 : possible (in reach)
                        // bits 23..16 : cx
                        // bits 15..8  : cy
                        // bits  7..0  : cz
} bblock;

struct {
    int active;                     // build is in progress
    uint64_t last_placement;        // last time we placed any block
    lh_arr_declare(bblock, all);    // all blocks scheduled for build
    int inreach[256];               // blocks in reach (indexes to .all)
    int ninr;                       // number of blocks in reach
} build;

// does a block need to be destroyed before we can place in its position?
static inline int is_solid(int type) {
    return !(type==0 ||                  // air
             type==0x08 || type==0x09 || // water
             type==0x0a || type==0x0b || // lava
             type==0x1f ||               // tallgrass  
             //type==0x06 ||               // saplings
             type==0x33                  // fire
             );
}

// try to find a block in the inreach[] that can be build with the specific inventory slot
int find_buildable(uint64_t ts, int sid) {
    int bid  = gs.inventory.slots[sid].id;
    int meta = gs.inventory.slots[sid].damage;

    int idx;
    bblock *bb;
    for(idx=0; idx<build.ninr; idx++) {
        bb = P(build.all) + build.inreach[idx];

        if (bb->bid != bid) // wrong block ID
            continue;

        if ((ITEMS[bb->bid].flags & I_MTYPE) && bb->meta!=meta) // wrong subtype
            continue;

        if ((ts-bb->ts)>MIN_BLOCK_DELAY)
            break;
    }

    if (idx == build.ninr)
        return -1; // could not find anything suitable

    return idx;
}

void switch_slot(int slot, lh_buf_t *server) {
    uint8_t pkt[4096], *p;

    p=pkt;
    write_varint(p,PID(CP_HeldItemChange));
    write_short(p,(short)slot);
    write_packet(pkt, p-pkt, server);
}

void autobuild(lh_buf_t *server) {
        
    uint64_t ts = gettimestamp();
    if ((ts-build.last_placement)<MIN_BUILD_DELAY) return;

    if (build.ninr<=0) return; // no blocks in reach

    int sid = gs.held+36;

    int idx = find_buildable(ts, sid);
    //printf("found idx=%d for sid=%d\n",idx,sid);
    if (idx < 0) {
        // current slot cannot be used, try to find another block type in the inventory bar

        int i;
        // note: only slots 3..7 are searched, others are for the tools and food
        // TODO: make this configurable
        for(i=3; i<=7; i++) {
            sid = i+36;
            idx = find_buildable(ts, sid);
            //printf("found idx=%d for sid=%d (searching in slots)\n",idx,sid);
            if (idx>=0) break;
        }
    }

    if (idx<0) return; // nothing could be found

    bblock *bb = P(build.all) + build.inreach[idx];

    if (sid != gs.held+36)
        switch_slot(sid-36, server);

#if 0
    printf("Placing at %d,%d,%d %08x %08x %08x %08x %08x %08x\n",
           bb->x,bb->y,bb->z,
           bb->place[0],bb->place[1],bb->place[2],bb->place[3],bb->place[4],bb->place[5]
           );
#endif
        
    uint8_t pkt[4096], *p;

    // place block
    p = pkt;
    write_varint(p,PID(CP_PlayerBlockPlacement));

    int dir;
    for(dir=0; dir<6; dir++) {
        if (bb->place[dir] & 0x0f000000) {
            // coords of the neighbor block
            switch (dir) {
                case 0: //UP
                    write_int(p,bb->x);         
                    write_char(p,(char)bb->y+1);
                    write_int(p,bb->z);
                    break;
                case 1: //DOWN
                    write_int(p,bb->x);         
                    write_char(p,(char)bb->y-1);
                    write_int(p,bb->z);
                    break;
                case 2: //SOUTH
                    write_int(p,bb->x);         
                    write_char(p,(char)bb->y);
                    write_int(p,bb->z+1);
                    break;
                case 3: //NORTH
                    write_int(p,bb->x);         
                    write_char(p,(char)bb->y);
                    write_int(p,bb->z-1);
                    break;
                case 4: //EAST
                    write_int(p,bb->x+1);         
                    write_char(p,(char)bb->y);
                    write_int(p,bb->z);
                    break;
                case 5: //WEST
                    write_int(p,bb->x-1);         
                    write_char(p,(char)bb->y);
                    write_int(p,bb->z);
                    break;
            }

            write_char(p,dir);      // direction

            write_short(p,0xffff);  //TODO: write proper slot

            write_char(p,(bb->place[dir]>>16)&0xff); // cx
            write_char(p,(bb->place[dir]>>8)&0xff);  // cy
            write_char(p,(bb->place[dir])&0xff);     // cz

            bb->ts = ts;
            build.last_placement = ts;

            break;
        }
    }

    write_packet(pkt, p-pkt, server);
    
    // wave arm
    p = pkt;
    write_varint(p,PID(CP_Animation));
    write_int(p,gs.own.id);
    write_char(p,0x01);
    write_packet(pkt, p-pkt, server);

    // switch slot back
    if (sid != gs.held+36)
        switch_slot(gs.held, server);
}

void build_request(char **words, lh_buf_t *client) {
    char reply[32768];
    reply[0]=0;

    if (!words[1]) {
        sprintf(reply, "Usage: build <type> [ parameters ... ] or build cancel");
    }
    else if (!strcmp(words[1], "cancel")) {
        clear_autobuild();
        sprintf(reply, "Current build canceled");
        opt.build = 0;
    }
    else if (!strcmp(words[1], "floor")) {
        int xsize,zsize;
        if (!words[2] || !words[3] || 
            sscanf(words[2],"%d",&xsize)!=1 || 
            sscanf(words[3],"%d",&zsize)!=1 ) {
            sprintf(reply, "Usage: build floor <xsize> <zsize> [ypos] [u|d]");
        }
        else {
            int x = gs.own.x>>5;
            int y = (gs.own.y>>5)-1; // coords of the block just below your feet
            int z = gs.own.z>>5;

            if (words[4]) sscanf(words[4],"%d",&y);
            
            char slab = 'm';
            if (words[5]) {
                if (sscanf(words[5],"%c",&slab)!=1) {
                    sprintf(reply, "Usage: build floor <xsize> <zsize> [ypos] [u|d]");
                    chat_message(reply, client, NULL);
                    return;
                }
                if (slab != 'u' && slab != 'd') {
                    sprintf(reply, "Usage: build floor <xsize> <zsize> [ypos] [u|d]");
                    chat_message(reply, client, NULL);
                    return;
                }
            }   

            clear_autobuild(); // cancel any current build

            int bid = gs.inventory.slots[gs.held+36].id;
            if (bid == 0xffff) {
                sprintf(reply, "You need to hold an item to start building.");
                chat_message(reply, client, NULL);
                return;
            }
            int meta = gs.inventory.slots[gs.held+36].damage;

            int i,j,dir;
            int dx = SGN(xsize);
            int dz = SGN(zsize);
            int n=0;

            printf("Building with the block ID %d (in slot %d (%d))\n", bid, gs.held, gs.held+36);

            for(i=0; i<abs(xsize); i++) {
                for(j=0; j<abs(zsize); j++) {
                    bblock * bb = lh_arr_new_c(GAR(build.all));
                    bb->x = x+i*dx;
                    bb->y = y;
                    bb->z = z+j*dz;
                    bb->bid = bid;
                    bb->meta = meta;

                    switch (slab) {
                        case 'u':
                            bb->place[DIR_UP]    = 0x10080008;
                            bb->place[DIR_SOUTH] = 0x10080c00;
                            bb->place[DIR_NORTH] = 0x10080c10;
                            bb->place[DIR_EAST]  = 0x10000c08;
                            bb->place[DIR_WEST]  = 0x10100c08;
                            break;
                        case 'd':
                            bb->place[DIR_DOWN]  = 0x10081008;
                            bb->place[DIR_SOUTH] = 0x10080400;
                            bb->place[DIR_NORTH] = 0x10080410;
                            bb->place[DIR_EAST]  = 0x10000408;
                            bb->place[DIR_WEST]  = 0x10100408;
                            break;
                        default:
                            bb->place[DIR_UP]    = 0x10080008;
                            bb->place[DIR_DOWN]  = 0x10081008;
                            bb->place[DIR_SOUTH] = 0x10080800;
                            bb->place[DIR_NORTH] = 0x10080810;
                            bb->place[DIR_EAST]  = 0x10000808;
                            bb->place[DIR_WEST]  = 0x10100808;
                            break;
                    }

                    //TODO: block ID
#if 0
                    printf("Scheduled block at %d,%d,%d %08x %08x %08x %08x %08x %08x\n",bb->x,bb->y,bb->z,
                           bb->place[0],bb->place[1],bb->place[2],bb->place[3],bb->place[4],bb->place[5]);
#endif
                    n++;
                }
            }
            sprintf(reply,"Scheduled %d blocks to build",n);
            opt.build = 1;
        }        
    }

    if (reply[0])
        chat_message(reply, client, NULL);
}

void build_process(lh_buf_t *client) {
    lh_clear_obj(build.inreach);
    build.ninr = 0;

    // get a cuboid in range
    int x = gs.own.x>>5;
    int y = (gs.own.y>>5)+1;
    int z = gs.own.z>>5;

    int Xl = (x>>4)-1;
    int Xh = Xl+2;
    int xoff = Xl*16;

    int Zl = (z>>4)-1;
    int Zh = Zl+2;
    int zoff = Zl*16;

    int yl = MAX(y-REACH_RANGE-1,1);
    int yh = MIN(y+REACH_RANGE+1,255);

    uint8_t *data = export_cuboid(Xl,Xh,Zl,Zh,yl,yh);

    int bid = gs.inventory.slots[gs.held+36].id;
    
    // detect which blocks are OK to place in
    int i;
    for(i=0; i<C(build.all) && build.ninr<256; i++) {
        bblock * bb = P(build.all)+i;


        //if (bb->bid != bid)
        //    continue; //TODO: allow switching to a different inv slot
                      // for now, we just don't allow building if we hold wrong material

        if (bb->x == x && bb->z == z && (bb->y == y || bb->y == y-1))
            continue; // don't try to build at our own position

        if (sqdist(x,y,z,bb->x,bb->y,bb->z) > SQ(REACH_RANGE))
            continue; // this block is too far away

        int cx = bb->x-xoff;
        int cz = bb->z-zoff;

        uint8_t *ydata = data+48*48*(bb->y-yl);
        uint8_t *b = ydata+cx+cz*48;

        // is the block type suitable to build in ?
        if (!is_solid(*b)) {
            int build_ok=0;

            // check which directions are suitable
            if ((bb->place[DIR_UP   ]&0xf0000000) && is_solid(b[48*48]))  {bb->place[DIR_UP   ] |= 0x01000000; build_ok++;}
            if ((bb->place[DIR_DOWN ]&0xf0000000) && is_solid(b[-48*48])) {bb->place[DIR_DOWN ] |= 0x01000000; build_ok++;}
            if ((bb->place[DIR_SOUTH]&0xf0000000) && is_solid(b[48]))     {bb->place[DIR_SOUTH] |= 0x01000000; build_ok++;}
            if ((bb->place[DIR_NORTH]&0xf0000000) && is_solid(b[-48]))    {bb->place[DIR_NORTH] |= 0x01000000; build_ok++;}
            if ((bb->place[DIR_EAST ]&0xf0000000) && is_solid(b[1]))      {bb->place[DIR_EAST ] |= 0x01000000; build_ok++;}
            if ((bb->place[DIR_WEST ]&0xf0000000) && is_solid(b[-1]))     {bb->place[DIR_WEST ] |= 0x01000000; build_ok++;}

            // if any of the directions is suitable for building, put it into inreach array
            if (build_ok)
                build.inreach[build.ninr++] = i;
        }
    }

    //printf("%d blocks in reach\n",build.ninr);

    build.active = (build.ninr>0);

    free(data);
}

////////////////////////////////////////////////////////////////////////////////

/*

#br start
#br stop
#br build
#br dump
#br save <name>
#br load <name>

*/

struct {
    int state;
    lh_arr_declare(bblock,blocks);

    int pset;     // pivot set
    int px,py,pz; // pivot block coords
    int pdir;     // pivot direction
} brec;

#define BREC_IDLE   0
#define BREC_REC    1
#define BREC_BUILD  2
#define BREC_SUSP   3


int brec_save(const char *fname) {
    ssize_t wlen = lh_save(fname, (uint8_t *)P(brec.blocks), C(brec.blocks)*sizeof(bblock));
    return (wlen>=0)?1:0;
}

int brec_load(const char *fname) {
    uint8_t * buf = NULL;
    ssize_t wlen = lh_load_alloc(fname, &buf);
    if (wlen<0) return 0;

    ssize_t n = wlen/sizeof(bblock);

    lh_arr_free(GAR(brec.blocks));
    lh_arr_allocate(GAR(brec.blocks),n);
    memcpy(P(brec.blocks),buf,wlen);
    free(buf);

    return 1;
}


void build_recorder(char **words, lh_buf_t *client) {
    char buf[4096];

    if (!words[1]) {
        sprintf(buf, "BREC: state=%d, recorded=%zd",brec.state,brec.C(blocks));
        chat_message(buf, client, "yellow");
        return;
    }

    if (!strcmp(words[1], "start")) {
        lh_arr_free(GAR(brec.blocks));
        lh_clear_obj(brec);
        brec.state = BREC_REC;
        chat_message("BREC: recording, place the pivot block to start", client, "yellow");
        return;
    }

    if (!strcmp(words[1], "stop")) {
        brec.state = BREC_IDLE;
        chat_message("BREC: idle", client, "yellow");
        return;
    }

    if (!strcmp(words[1], "build")) {
        brec.state = BREC_BUILD;
        clear_autobuild();
        chat_message("BREC: building, place the pivot block to start", client, "yellow");
        return;
    }

    if (!strcmp(words[1], "dump")) {
        sprintf(buf, "BREC: state=%d, recorded=%zd, dumping to stdout",brec.state,brec.C(blocks));
        chat_message(buf, client, "yellow");
        if (brec.pset) {
            sprintf(buf, "pivot at %d,%d,%d dir=%d",brec.px,brec.py,brec.pz,brec.pdir);
            chat_message(buf, client, "yellow");
        }

        int i,j;
        for(i=0; i<C(brec.blocks); i++) {
            bblock * bb = P(brec.blocks)+i;
            printf("%3d : %2x (%-30s) at [ %6d %6d %3d ] ",
                   i, bb->bid, ITEMS[bb->bid].name?ITEMS[bb->bid].name:"<unknown>",
                   bb->x, bb->z, bb->y);
            for(j=0; j<6; j++)
                printf(" %08x",bb->place[j]);
            printf("\n");
        }

        return;
    }

    if (!strcmp(words[1], "suspend")) {
        if (brec.state == BREC_REC) {
            brec.state = BREC_SUSP;
            chat_message("BREC: suspended", client, "yellow");
        }
        else if (brec.state == BREC_SUSP) {
            brec.state = BREC_REC;
            chat_message("BREC: resumed", client, "yellow");
        }
        return;
    }

    if (!strcmp(words[1], "save")) {
        if (!words[2]) {
            chat_message("usage: #br save <filename>", client, "yellow");
            return;
        }

        if (brec_save(words[2]))
            sprintf(buf,"saved a brec segment with %zd blocks to %s",C(brec.blocks),words[2]);
        else
            sprintf(buf,"could not save brec state to %s",words[2]);
        chat_message(buf, client, "yellow");
        return;
    }

    if (!strcmp(words[1], "load")) {
        if (!words[2]) {
            chat_message("usage: #br load <filename>", client, "yellow");
            return;
        }

        if (brec_load(words[2]))
            sprintf(buf,"loaded a brec segment with %zd blocks from %s",C(brec.blocks),words[2]);
        else
            sprintf(buf,"could not load brec state from %s",words[2]);
        chat_message(buf, client, "yellow");
        return;
    }
}

// generate the direction constant from our yaw
int calc_direction(float yaw) {
    int lx=-(int)(65536*sin(yaw/180*M_PI));
    int lz=(int)(65536*cos(yaw/180*M_PI));

    if (abs(lx) > abs(lz))
        return (lx<0) ? DIR_WEST : DIR_EAST;
    else
        return (lz<0) ? DIR_NORTH : DIR_SOUTH;
}

void brec_record(int x, uint8_t y, int z, char dir, 
                 uint16_t bid, uint16_t damage, 
                 char cx, char cy, char cz) {

    if (dir == -1) return; // ignore fake placements

    // coordinates of the block being placed
    int bx=x - (dir==DIR_EAST)  + (dir==DIR_WEST);
    int by=y - (dir==DIR_UP)    + (dir==DIR_DOWN);
    int bz=z - (dir==DIR_SOUTH) + (dir==DIR_NORTH);

    if (!brec.pset) {
        brec.pset = 1;
        brec.px = bx;
        brec.py = by;
        brec.pz = bz;
        brec.pdir = calc_direction(gs.own.yaw);

        printf("Pivot set, at %d %d %d , dir=%d\n",bx,by,bz,brec.pdir);
    }

    // storing the recorded block in the list
    bblock * bb = lh_arr_new_c(GAR(brec.blocks));

    // block type and optional meta 
    bb->bid = bid;
    bb->meta = damage;

    // the y coordinate stays same
    bb->y = by-brec.py;

    // the x and z coordinates are stored in terms of 
    // 'forward' and 'right' directions from the pivot

    switch (brec.pdir) {
        case DIR_NORTH:
            bb->x = -(bz-brec.pz);
            bb->z = bx-brec.px;
            break;
        case DIR_SOUTH:
            bb->x = bz-brec.pz;
            bb->z = -(bx-brec.px);
            break;
        case DIR_EAST:
            bb->x = bx-brec.px;
            bb->z = bz-brec.pz;
            break;
        case DIR_WEST:
            bb->x = -(bx-brec.px);
            bb->z = -(bz-brec.pz);
            break;
    }

    bb->place[DIR_UP]    = 0x10080008;
    bb->place[DIR_DOWN]  = 0x10081008;
    bb->place[DIR_SOUTH] = 0x10080800;
    bb->place[DIR_NORTH] = 0x10080810;
    bb->place[DIR_EAST]  = 0x10000808;
    bb->place[DIR_WEST]  = 0x10100808;

    //TODO: come up with a better solution to place direction-dependent blocks

#if 0
    if (ITEMS[bid].flags & I_MPOS) {
        // block meta is position-dependent, only allow
        // this block to be placed as specified by the player
        bb->place[dir] = 0x10000000|(cx<<16)|(cy<<8)|cz;
    }
    else {
        // this block can be placed any way
    }
#endif
}

void brec_place_pivot(int x, uint8_t y, int z, int dir) {
    if (dir == -1) return; // ignore fake placements

    // coordinates of the block being placed
    int bx=x - (dir==DIR_EAST)  + (dir==DIR_WEST);
    int by=y - (dir==DIR_UP)    + (dir==DIR_DOWN);
    int bz=z - (dir==DIR_SOUTH) + (dir==DIR_NORTH);
    int pdir = calc_direction(gs.own.yaw);

    printf("Pivot set, at %d %d %d , dir=%d\n",bx,by,bz,pdir);

    int i;
    for(i=0; i<C(brec.blocks); i++) {
        bblock * rb = P(brec.blocks) + i;
        bblock * bb = lh_arr_new_c(GAR(build.all));
        *bb = *rb;

        bb->y += by;
        bb->x = bx;
        bb->z = bz;

        switch (pdir) {
            case DIR_NORTH:
                bb->x +=rb->z;
                bb->z -=rb->x;
                break;
            case DIR_SOUTH:
                bb->x -=rb->z;
                bb->z +=rb->x;
                break;
            case DIR_EAST:
                bb->x +=rb->x;
                bb->z +=rb->z;
                break;
            case DIR_WEST:
                bb->x -=rb->x;
                bb->z -=rb->z;
                break;
        }
    }

    brec.state = BREC_IDLE;
    opt.build = 1;
}

void clear_autobuild() {
    lh_arr_free(GAR(build.all));
    lh_clear_obj(build);
}

////////////////////////////////////////////////////////////////////////////////

int handle_async(lh_buf_t *client, lh_buf_t *server) {
    if (opt.autokill) autokill(server);
    autobuild(server);    
    return 0;
}

////////////////////////////////////////////////////////////////////////////////
// Find Tunnels

#define TUNNEL_TRESHOLD 0.4

void find_tunnels(lh_buf_t *answer, int l, int h) {
    int i;

    int Xl,Xh,Zl,Zh;
    int nc = get_chunks_dim(&Xl,&Xh,&Zl,&Zh);
    uint8_t *data = export_cuboid(Xl,Xh,Zl,Zh,l,h);

    int xsz = (Xh-Xl+1)*16;
    int zsz = (Zh-Zl+1)*16;
    int ysz = (h-l+1);

    char reply[32768];
    //sprintf(reply,"got %d chunks (%d,%d) to (%d,%d)",nc,Xl,Zl,Xh,Zh);
    //chat_message(reply, answer, "blue");

    // search for tunnels

    for(i=l; i<=h; i++) {
        uint8_t * slice = data+(i-l)*xsz*zsz;

        int x,z;
        for(x=0; x<xsz; x++) {
            int nair=0, nfilled=0;
            uint8_t * p=slice+x;
            for(z=0; z<zsz; z++) {
                if (*p!=0xff) {
                    if (*p)
                        nfilled++;
                    else
                        nair++;
                }
                p += xsz;
            }

            //printf("level=%d x=%d : %d/%d\n",i,x+Xl*16,nair,nfilled);
            if ((float)(nair) > (float)(nair+nfilled)*TUNNEL_TRESHOLD) {
                sprintf(reply,"Tunnel NS at level=%d x=%d  (%d/%d)",i,x+Xl*16,nair,nfilled);
                chat_message(reply, answer, "yellow");
            }
        }
    }

    for(i=l; i<=h; i++) {
        uint8_t * slice = data+(i-l)*xsz*zsz;
        int x,z;

        for(z=0; z<zsz; z++) {
            int nair=0, nfilled=0;
            uint8_t * p=slice+z*xsz;

            for(x=0; x<xsz; x++) {
                if (*p!=0xff) {
                    if (*p)
                        nfilled++;
                    else
                        nair++;
                }
                p++;
            }

            //printf("level=%d x=%d : %d/%d\n",i,x+Xl*16,nair,nfilled);
            if ((float)(nair) > (float)(nair+nfilled)*TUNNEL_TRESHOLD) {
                sprintf(reply,"Tunnel WE at level=%d z=%d  (%d/%d)",i,z+Zl*16,nair,nfilled);
                chat_message(reply, answer, "yellow");
            }
        }
    }

    free(data);
}

void salt_request(lh_buf_t *server, int x, int y, int z) {
    uint8_t pkt[4096], *p;

    // place block
    p = pkt;
    write_varint(p,0x08); // PlayerBlockPlacement
    write_int(p,x);
    write_char(p,y);
    write_int(p,z);
    write_char(p,4);
    write_short(p,0xffff); //TODO: proper slot data
    write_char(p,1);       
    write_char(p,7);       
    write_char(p,4);        
    write_packet(pkt, p-pkt, server);

    // wave arm
    p = pkt;
    write_varint(p,0x0a); // Animation
    write_int(p,gs.own.id);
    write_char(p,0x01);
    write_packet(pkt, p-pkt, server);
}

////////////////////////////////////////////////////////////////////////////////

int process_message(const char *msg, lh_buf_t *forw, lh_buf_t *retour) {
    if (msg[0] != '#') return 0;

    // tokenize
    char *words[256];
    CLEAR(words);
    int w=0;

    char wbuf[4096];
    strncpy(wbuf, msg+1, sizeof(wbuf));
    char *wsave;

    char *wstr = wbuf;
    do {
        words[w++] = strtok_r(wstr, " ", &wsave);
        wstr = NULL;
    } while(words[w-1]);
    w--;

    if (w==0) return 0;

    uint8_t reply[32768];
    reply[0] = 0;

    if (!strcmp(words[0],"test")) {
        sprintf(reply,"Chat test response");
    }
    else if (!strcmp(words[0],"entities")) {
        sprintf(reply,"Tracking %zd entities",gs.C(entity));
    }
    else if (!strcmp(words[0],"autokill")) {
        opt.autokill = !opt.autokill;
        sprintf(reply,"Autokill is %s",opt.autokill?"enabled":"disabled");
    }
    else if (!strcmp(words[0],"grind")) {
        int maxlevel=30;
        int start = 1;

        if (words[1]) {
            if (!strcmp(words[1],"stop")) {
                opt.grinding = 0;
                opt.autokill = 0;
                sprintf(reply,"Grinding is stopped");
                start = 0;
            }
            else if (sscanf(words[1],"%d",&maxlevel)!=1) {
                maxlevel = 30;
            }
        }

        if (start) {
            opt.grinding = 1;
            opt.autokill = 1;
            opt.maxlevel = maxlevel;
            sprintf(reply,"Grinding to level %d",opt.maxlevel);
        }
    }
    else if (!strcmp(words[0],"ft")) {
        int ll = 119;
        int lh = 122;
        int error = 0;
        if (words[1]) {
            if (sscanf(words[1],"%d",&ll)!=1)
                error++;

            if (words[2]) {
                if (sscanf(words[2],"%d",&lh)!=1)
                    error++;
            }
        }

        if (ll<0 || lh<0 || lh<ll || lh>((gs.current_dimension==DIM_NETHER)?126:254))
            error++;

        if (error)
            sprintf(reply, "incorrect level range specified");
        else
            find_tunnels(retour,ll,lh);
    }
    else if (!strcmp(words[0],"map")) {
        int error=0;
        int y = gs.own.y>>5;

        if (words[1]) {
            if (sscanf(words[1],"%d",&y)!=1)
                error++;
        }

        if (error) {
            sprintf(reply, "incorrect level specified");
        }
        else {
            int xp = gs.own.x>>5;
            int zp = gs.own.z>>5;
            int X = gs.own.x>>9;
            int Z = gs.own.z>>9;
            uint8_t *map = export_cuboid(X-5,X+5,Z-5,Z+5,y,y);

            int xoff = (X-5)*16;
            int zoff = (Z-5)*16;

            int x,z;
            printf("MAP x=%d:%d z=%d:%d y=%d\n",(X-5)*16,(X+6)*16-1,(Z-5)*16,(Z+6)*16-1,y);
            for(z=(5*16); z<(7*16); z++) {
                uint8_t * p = map+z*(11*16)+5*16;
                printf("%s%6d  ",ANSI_CLEAR,z);
                for(x=(5*16); x<(7*16); x++,p++) {
                    if (x+xoff==xp && z+zoff==zp)
                        printf("%s",ANSI_PLAYER);
                    else
                        printf("%s",ANSI_BLOCK[*p]);
                }
                printf("%s\n",ANSI_CLEAR);
            }
            free(map);
        }
    }
    else if (!strcmp(words[0],"build")) {
        build_request(words, retour);
    }
    else if (!strcmp(words[0],"br")) {
        build_recorder(words, retour);
    }
    else if (!strcmp(words[0],"salt")) {
        int x,y,z;
        if (!words[1] || !words[2] || !words[3] || 
            sscanf(words[1],"%d",&x)!=1 || 
            sscanf(words[2],"%d",&y)!=1 || 
            sscanf(words[3],"%d",&z)!=1 ) {
            sprintf(reply, "Usage: #salt x y z");
        }
        salt_request(retour,x,y,z);
        
    }
    else if (!strcmp(words[0],"holeradar")) {
        opt.holeradar = !opt.holeradar;
        sprintf(reply,"Hole radar is %s",opt.holeradar?"enabled":"disabled");
    }
    else if (!strcmp(words[0],"pinv")) {
        print_inventory();
    }

    if (reply[0])
        chat_message(reply, retour, NULL);

    return 1;
}
#endif

#if 0
    uint8_t *p = ptr;
    uint32_t type = lh_read_varint(p);
    uint32_t stype = ((STATE_PLAY<<24)|(is_client<<28)|(type&0xffffff));

#if 0
#endif

    uint8_t output[65536];
    uint8_t *w = output;

#if 0
    switch (stype) {
        case SP_SpawnPlayer: {
            Rvarint(eid);
            Rstr(uuid);
            Rstr(name);
            Rint(x);
            Rint(y);
            Rint(z);
            Rchar(yaw);
            Rchar(pitch);
            Rshort(item);
            //TODO: metadata

            char msg[32768];
            sprintf(msg, "Player %s at %d,%d,%d",name,(int)x>>5,(int)y>>5,(int)z>>5);
            chat_message(msg, tx, "blue");
            write_packet(ptr, len, tx);
            break;
        }

        case SP_SoundEffect: {
            Rstr(name);
            Rint(x);
            Rint(y);
            Rint(z);
            Rfloat(volume);
            Rchar(pitch);
            if (!strcmp(name,"ambient.weather.thunder")) {
                printf("**** THUNDER ****\n"
                       "coords=%d,%d,%d vol=%.4f pitch=%d\n",
                       x/8,y/8,z/8,volume,pitch);
                drop_connection();
                //close(mitm.ms);
            }
            if (strcmp(name,"mob.sheep.say") && strcmp(name,"mob.sheep.step"))
                write_packet(ptr, len, tx);
            break;
        }

        case SP_Effect: {
            Rint(efid);
            Rint(x);
            Rchar(y);
            Rint(z);
            Rint(data);
            Rchar(disrv);
            if (efid == 1013) {
                printf("**** Wither Spawn ****  efid=%d data=%d bcoord=%d:%d:%d %s\n",
                       efid,data,x,y,z,disrv?"disable relative volume":"");
            }
            write_packet(ptr, len, tx);
            break;
        }

        case SP_SetExperience: {
            Rfloat(bar);
            Rshort(level);
            Rshort(exp);

            if (opt.grinding && level >= opt.maxlevel) {
                opt.grinding = 0;
                opt.autokill = 0;

                char msg[4096];
                sprintf(msg, "Grinding finished at level %d",level);
                chat_message(msg, tx, "green");
            }
            write_packet(ptr, len, tx);
            break;
        }

        case SP_PlayerPositionLook:
        case CP_PlayerPositionLook:
        case CP_PlayerPosition:
        case CP_PlayerLook:
        case SP_MultiBlockChange:
        case SP_BlockChange: {
            if (opt.holeradar) {
                int x = gs.own.x>>5;
                int y = gs.own.y>>5;
                int z = gs.own.z>>5;
                int yaw = (int)gs.own.yaw/90;

                if (x!= hr_last_x || y!= hr_last_y || 
                    z!= hr_last_z || yaw!= hr_last_yaw ) {
                    hr_last_x = x;
                    hr_last_y = y;
                    hr_last_z = z;
                    hr_last_yaw = yaw;
                    hole_radar(is_client?bx:tx);
                }
            }
            if (opt.build) {
                build_process(is_client?bx:tx);
            }

            write_packet(ptr, len, tx);
            break;
        }

        case CP_ChatMessage: {
            Rstr(msg);
            if (msg[0] == '#') {
                if (process_message(msg, tx, bx))
                    break;
            }

            // if it was a normal chat message, just forward it
            write_packet(ptr, len, tx);
            break;
        }

        ////////////////////////////////////////////////////////////////////////

        case CP_HeldItemChange: {
            Rshort(sid);
            gs.held = sid;
            //printf("HeldItemChange (C) sid=%d\n",sid);
            write_packet(ptr, len, tx);
            break;
        }
            
        case SP_WindowItems: {
            Rchar(wid);
            Rshort(nslots);
            
            int i;
            //printf("WindowItems : %d slots\n",nslots);
            for(i=0; i<nslots; i++) {
                Rslot(s);
                //printf("  %2d: iid=%-3d count=%-2d dmg=%-5d dlen=%d bytes\n", i, s.id, s.count, s.damage, s.dlen);
                if (s.dlen!=0 && s.dlen!=0xffff) {
                    uint8_t buf[256*1024];
                    ssize_t olen = lh_gzip_decode_to(s.data, s.dlen, buf, sizeof(buf));
                    //if (olen > 0) hexdump(buf, 128);
                }
            }
            write_packet(ptr, len, tx);
            break;
        }
            
        case SP_OpenWindow: {
            Rchar(wid);
            Rchar(invtype);
            Rstr(title);
            Rshort(nslots);
            Rchar(usetitle);
            //printf("OpenWindow: wid=%d type=%d title=%s nslots=%d usetitle=%d\n", wid, invtype, title, nslots, usetitle);
            write_packet(ptr, len, tx);
            break;
        }

        case SP_CloseWindow: {
            Rchar(wid);
            //printf("CloseWindow (S): wid=%d\n", wid);
            write_packet(ptr, len, tx);
            break;
        }

        case CP_CloseWindow: {
            Rchar(wid);
            //printf("CloseWindow (C): wid=%d\n", wid);
            write_packet(ptr, len, tx);
            break;
        }

        case SP_ConfirmTransaction: {
            Rchar(wid);
            Rshort(action);
            Rchar(accepted);
            //printf("ConfirmTransaction: wid=%d action=%04x accepted=%d\n",wid,action,accepted);
            write_packet(ptr, len, tx);
            break;
        }

        case CP_ClickWindow: {
            Rchar(wid);
            Rshort(sid);
            Rchar(button);
            Rshort(action);
            Rchar(mode);
            Rslot(s);

            //printf("ClickWindow: wid=%d action=%04x sid=%d mode=%d button=%d\n",wid, action, (short)sid, mode, button);
            write_packet(ptr, len, tx);
            break;
        }

        ////////////////////////////////////////////////////////////////////////

        case CP_PlayerBlockPlacement: {
            Rint(x);
            Rchar(y);
            Rint(z);
            Rchar(dir);
            Rslot(held);
            Rchar(cx);
            Rchar(cy);
            Rchar(cz);

            //printf("PlayerBlockPlacement %d,%d,%d dir=%d c=%d,%d,%d yaw=%.1f (%d)\n",
            //       x,y,z,dir,cx,cy,cz,gs.own.yaw,calc_direction(gs.own.yaw));

            int forward = 1;
            switch (brec.state) {
                case BREC_REC:
                    brec_record(x,y,z,dir,held.id,held.damage,cx,cy,cz);
                    break;
                case BREC_BUILD:
                    brec_place_pivot(x,y,z,dir);
                    forward = 0;
                    break;
                    
            }

            if (forward)
                write_packet(ptr, len, tx);
            break;
        }

        ////////////////////////////////////////////////////////////////////////
#endif
        default: {
            // by default, just forward the packet as is
            write_packet(ptr, len, tx);
        }
    }

}
#endif
#if 0
void chat_message(const char *str, lh_buf_t *buf, const char *color);

void hole_radar(lh_buf_t *client);
void find_tunnels(lh_buf_t *answer, int l, int h);
void autokill(lh_buf_t *server);
void salt_request(lh_buf_t *server, int x, int y, int z);

void autobuild(lh_buf_t *server);
void clear_autobuild();
void build_request(char **words, lh_buf_t *client);
void build_process(lh_buf_t *client);
#endif
