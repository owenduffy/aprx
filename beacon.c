/* **************************************************************** *
 *                                                                  *
 *  APRX -- 2nd generation APRS iGate and digi with                 *
 *          minimal requirement of esoteric facilities or           *
 *          libraries of any kind beyond UNIX system libc.          *
 *                                                                  *
 * (c) Matti Aarnio - OH2MQK,  2007-2014                            *
 *                                                                  *
 * **************************************************************** */

#include "aprx.h"

struct beaconmsg {
	time_t nexttime;
	int    interval;
	const struct aprx_interface *interface;
	const char *src;
	const char *dest;
	const char *via;
	const char *msg;
	const char *filename;
	const char *execfile;
	int8_t	    beaconmode; // -1: net only, 0: both, +1: radio only
	int8_t	    timefix;
	int         timeout;
};

struct beaconset {
	struct beaconmsg **beacon_msgs;

  	struct timeval beacon_nexttime;
	float  beacon_cycle_size;

	int beacon_msgs_count;
	int beacon_msgs_cursor;

	int    exec_pid;
	int    exec_fd;
        time_t exec_deadline; // seconds
  	char  *exec_buf;
	int    exec_buf_length;
	int    exec_buf_space;
  	struct beaconmsg *exec_bm;
};

static struct beaconset **bsets;
static int bsets_count;

static void beacon_it(struct beaconset *bset, struct beaconmsg *bm);


static void beacon_reset(struct beaconset *bset)
{
	tv_timeradd_seconds(&bset->beacon_nexttime, &tick, 30);	// start 30 seconds from now
	bset->beacon_msgs_cursor = 0;
}

static void beacon_set(struct configfile *cf,
                       const char *p1,
                       char *str,
                       const int beaconmode,
                       struct beaconset *bset)
{
	const char *srcaddr  = NULL;
	const char *destaddr = NULL;
	const char *via      = NULL;
	const char *name     = NULL;
	int buflen = strlen(p1) + strlen(str ? str : "") + 10;
	char *buf  = alloca(buflen);
	const char *to   = NULL;
	char *code = NULL;
	const char *lat  = NULL;
	const char *lon  = NULL;
	char *comment = NULL;
	char *type    = NULL;
	const struct aprx_interface *aif = NULL;
	int has_fault = 0;

	struct beaconmsg *bm = calloc(1, sizeof(*bm));

	*buf = 0;

	if (debug) {
	  printf("BEACON parameters: ");
	}

	while (*p1) {

		/* if (debug)
		   printf("p1='%s' ",p1); */

		if (strcmp(p1, "interface") == 0 ||
		    strcmp(p1, "to") == 0) {

			if (to != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			}

			to = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (beaconmode < 0) {
			  printf("%s:%d ERROR: beaconmode APRSIS is incompatible with beaconing to designated interface ('%s %s')\n",
				 cf->name, cf->linenum, p1, to);
			  has_fault = 1;
			  goto discard_bm; // sigh..
			}

			if (strcasecmp(to,"$mycall") == 0) {
				to = mycall;
			} else {
				config_STRUPPER((void*)to);
			}

			aif = find_interface_by_callsign(to);
			if ((aif != NULL) && (!aif->tx_ok)) {
				aif = NULL;  // Not an TX interface :-(
				if (debug)printf("\n");
				printf("%s:%d ERROR: beacon interface '%s' that is not a TX capable interface.\n",
				       cf->name, cf->linenum, to);
				has_fault = 1;
				goto discard_bm; // sigh..
			} else if (aif == NULL) {
				if (debug)printf("\n");
				printf("%s:%d ERROR: beacon interface '%s' that is not a known interface.\n",
				       cf->name, cf->linenum, to);
				has_fault = 1;
			}

			if (debug)
				printf("interface '%s' ", to);

		} else if (strcmp(p1, "srccall") == 0 ||
			   strcmp(p1, "for") == 0) {

			if (srcaddr != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			}
			srcaddr = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (strcasecmp(srcaddr,"$mycall") == 0) {
				srcaddr = mycall;
			} else {
				config_STRUPPER((void*)srcaddr);
			}

			// What about ITEM and OBJECT ?

			// if (!validate_callsign_input((char *) srcaddr),1) {
			//   if (debug)printf("\n");
			//   printf("Invalid rfbeacon FOR callsign");
			// }

			if (debug)
				printf("srccall '%s' ", srcaddr);

		} else if (strcmp(p1, "dstcall") == 0 ||
			   strcmp(p1, "dest") == 0) {

			if (destaddr != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			}
			destaddr = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			config_STRUPPER((void*)destaddr);

			if (debug)
				printf("dstcall '%s' ", destaddr);

		} else if (strcmp(p1, "via") == 0) {

			if (via != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			}
			via  = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			config_STRUPPER((void*)via);

			if (debug)
				printf("via '%s' ", via);

		} else if (strcmp(p1, "name") == 0) {

			if (name != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			}
			name  = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (debug)
				printf("name '%s' ", name);

		} else if (strcmp(p1, "item") == 0) {
			if (name != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			}
			if (type != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of type parameter\n",
				 cf->name, cf->linenum);
			}
			type = ")";
			name  = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (debug)
				printf("item '%s' ", name);

		} else if (strcmp(p1, "object") == 0) {
			if (name != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			}
			if (type != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of type parameter\n",
				 cf->name, cf->linenum);
			}
			type = ";";

			name  = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (debug)
				printf("object '%s' ", name);

		} else if (strcmp(p1, "type") == 0) {
			/* text up to .. 40 chars */

			if (type != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			}
			type = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);
			type = strdup(type);

			if (debug)
				printf("type '%s' ", type);
			if (type[1] != 0 || (type[0] != '!' &&
					     type[0] != '=' &&
					     type[0] != '/' &&
					     type[0] != '@' &&
					     type[0] != ';' &&
					     type[0] != ')')) {
			  has_fault = 1;
			  printf("%s:%d Sorry, packet constructor's supported APRS packet types are only: ! = / @ ; )\n",
				 cf->name, cf->linenum);
			}

		} else if (strcmp(p1, "$myloc") == 0) {
                	if (myloc_latstr != NULL) {
                          lat = myloc_latstr;
                          lon = myloc_lonstr;
                        } else {
                          has_fault = 1;
			  printf("%s:%d ERROR: $myloc has not been defined.\n",
				 cf->name, cf->linenum);

                        }
		} else if (strcmp(p1, "lat") == 0) {
			/*  ddmm.mmN   */

			if (lat != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			}
			lat = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (!has_fault && validate_degmin_input(lat, 90)) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Latitude input has bad format: '%s'\n",
				 cf->name, cf->linenum, lat);
			}

			if (debug)
				printf("lat '%s' ", lat);

		} else if (strcmp(p1, "lon") == 0) {
			/*  dddmm.mmE  */

			if (lon != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			}
			lon = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (validate_degmin_input(lon, 180)) {
			  has_fault = 1;
			  printf("Longitude input has bad format: '%s'\n",lon);
			}

			if (debug)
				printf("lon '%s' ", lon);

		} else if (strcmp(p1, "symbol") == 0) {
			/*   R&    */

			if (code != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			}
			code = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);
			if (strlen(code) != 2) {
			  has_fault = 1;
			  printf("Symbol code lenth is not exactly 2 chars\n");
			}

			if (debug)
				printf("symbol '%s' ", code);

		} else if (strcmp(p1, "comment") == 0) {
			/* text up to .. 40 chars */

			if (comment != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			}
			comment = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (debug)
				printf("comment '%s' ", comment);

		} else if (strcmp(p1, "raw") == 0) {

			p1 = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (bm->msg != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			} else
			  bm->msg = strdup(p1);

			// FIXME: validate the data with APRS parser...

			if (debug)
				printf("raw '%s' ", bm->msg);

		} else if (strcmp(p1, "file") == 0) {

			p1 = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (bm->filename != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			} else
			  bm->filename = strdup(p1);

			if (debug)
				printf("file '%s' ", bm->filename);

		} else if (strcmp(p1, "exec") == 0) {

			p1 = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			if (bm->execfile != NULL) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			} else
			  bm->execfile = strdup(p1);

                        // Set default timeout if not yet set.
                        if (bm->timeout == 0)
                          bm->timeout = 10;

			if (debug)
				printf("exec file '%s' ", bm->execfile);

		} else if (strcmp(p1, "timeout") == 0) {

			p1 = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);

			bm->timeout = atoi(p1);

			if (debug)
				printf("timeout %d ", bm->timeout);

		} else if (strcmp(p1, "timefix") == 0) {
			if (bm->timefix) {
			  has_fault = 1;
			  printf("%s:%d ERROR: Double definition of %s parameter\n",
				 cf->name, cf->linenum, p1);
			}
			bm->timefix = 1;
			if (debug)
				printf("timefix ");

		} else {

			has_fault = 1;
#if 0
			if (debug)
				printf("Unknown keyword: '%s'", p1);

			p1 = str;
			str = config_SKIPTEXT(str, NULL);
			str = config_SKIPSPACE(str);
#else
			/* Unknown keyword, a raw message ? */
			bm->msg = strdup(p1);

			if (debug)
				printf("ASSUMING raw '%s' ", bm->msg);

			break;
#endif
		}

		p1 = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);
	}
	if (debug)
		printf("\n");
	if (has_fault)
		goto discard_bm;

	if (aif == NULL && beaconmode >= 0) {
		if (debug)
			printf("%s:%d Note: Lacking 'interface' keyword for this beacon definition. Beaconing to all Tx capable interfaces + APRSIS (mode depending)\n",
			       cf->name, cf->linenum);
	}

/*
	if (srcaddr == NULL)
		srcaddr = mycall;

	if (srcaddr == NULL) {
		if (debug)
			printf("%s:%d Note: Lacking the 'for' keyword for this beacon definition.\n", cf->name, cf->linenum);
		has_fault = 1;
		goto discard_bm;
	}
*/

	if (destaddr == NULL)
		destaddr = tocall;

	bm->src       = srcaddr != NULL ? strdup(srcaddr) : NULL;
	bm->dest      = strdup(destaddr);
	bm->via       = via != NULL ? strdup(via) : NULL;
	bm->interface = aif;
	bm->beaconmode = beaconmode;

	if (!bm->msg && !bm->filename && !bm->execfile) {
		/* Not raw packet, perhaps composite ? */
		if (!type) type = "!";
		if (code && strlen(code) == 2 && lat && strlen(lat) == 8 &&
		    lon && strlen(lon) == 9) {
			if ( strcmp(type,"!") == 0 ||
			     strcmp(type,"=") == 0 ) {
				sprintf(buf, "%s%s%c%s%c%s", type, lat, code[0], lon,
					code[1], comment ? comment : "");
			} else if ( strcmp(type,"/") == 0 ||
				    strcmp(type,"@") == 0) {
				sprintf(buf, "%s111111z%s%c%s%c%s", type, lat, code[0], lon,
					code[1], comment ? comment : "");
			} else if ( strcmp(type,";") == 0 && name) { // Object
				sprintf(buf, ";%-9.9s*111111z%s%c%s%c%s", name, lat, code[0], lon,
					code[1], comment ? comment : "");

			} else if ( strcmp(type,")") == 0 && name) { // Item
				sprintf(buf, ")%-3.9s!%s%c%s%c%s", name, lat, code[0], lon,
					code[1], comment ? comment : "");
			}
			bm->msg = strdup(buf);
		} else {
			if (!code || (code && strlen(code) != 2))
				printf("%s:%d .. BEACON definition failure; symbol parameter missing or wrong size\n", cf->name, cf->linenum);
			if (!lat || (lat && strlen(lat) != 8))
				printf("%s:%d .. BEACON definition failure; lat(itude) parameter missing or wrong size\n", cf->name, cf->linenum);
			if (!lon || (lon && strlen(lon) != 9))
				printf("%s:%d .. BEACON definition failure; lon(gitude) parameter missing or wrong size\n", cf->name, cf->linenum);
			/* parse failure, abandon the alloc too */
			has_fault = 1;
			goto discard_bm;
		}
	}

	if (debug) {
	  switch (beaconmode) {
	  case 1:
	    printf("RFONLY");
	    break;
	  case 0:
	    printf("RF+NET");
	    break;
	  default:
	    printf("NETONLY");
	    break;
	  }
	  printf(" BEACON FOR ");
	  if (srcaddr == NULL)
	    printf("***>%s", destaddr);
	  else
	    printf("%s>%s",srcaddr,destaddr);
	  if (via != NULL)
	    printf(",%s", via);
	  if (bm->filename)
	    printf("'  file %s\n", bm->filename);
	  else
	    printf("'  '%s'\n", bm->msg);
	}

	/* realloc() works also when old ptr is NULL */
	bset->beacon_msgs = realloc(bset->beacon_msgs,
                                    sizeof(bm) * (bset->beacon_msgs_count + 3));

	bset->beacon_msgs[bset->beacon_msgs_count++] = bm;
	bset->beacon_msgs[bset->beacon_msgs_count] = NULL;
	
	if (bm->msg != NULL) {  // Make this into AX.25 UI frame
	                        // with leading control byte..
	  int len = strlen(bm->msg);
	  char *msg = realloc((void*)bm->msg, len+3); // make room
	  memmove(msg+2, msg, len+1); // move string end \0 also
	  msg[0] = 0x03;  // Control byte
	  msg[1] = 0xF0;  // PID 0xF0
	  bm->msg = msg;
	}

	beacon_reset(bset);

	if (0) {
	discard_bm:
	  if (bm->dest != NULL) free((void*)(bm->dest));
	  if (bm->msg  != NULL) free((void*)(bm->msg));
	  free(bm);
	}
	return;
}

static void free_beaconmsg(struct beaconmsg *bmsg) {
	if (bmsg == NULL) return;
        if (bmsg->src)  free((void*)bmsg->src);
        if (bmsg->dest) free((void*)bmsg->dest);
        if (bmsg->via)  free((void*)bmsg->via);
        if (bmsg->msg)  free((void*)bmsg->msg);
        if (bmsg->filename) free((void*)bmsg->filename);
        if (bmsg->execfile) free((void*)bmsg->execfile);
        free(bmsg);
}

static void free_beaconset(struct beaconset *bset) {
        int i;
	if (bset == NULL) return;
        for (i = 0; i < bset->beacon_msgs_count; ++i) {
          free_beaconmsg(bset->beacon_msgs[i]);
        }
        free(bset);
}

int beacon_config(struct configfile *cf)
{
	char *name, *param1;
	char *str = cf->buf;
	int   beaconmode = 0;
	int   has_fault  = 0;


        struct beaconset *bset = calloc(1, sizeof(*bset));
        bset->beacon_cycle_size = 20.0*60.0; // 20 minutes is the default

	while (readconfigline(cf) != NULL) {
		if (configline_is_comment(cf))
			continue;	/* Comment line, or empty line */

		// It can be severely indented...
		str = config_SKIPSPACE(cf->buf);

		name = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);
		config_STRLOWER(name);

		param1 = str;
		str = config_SKIPTEXT(str, NULL);
		str = config_SKIPSPACE(str);

		if (strcmp(name, "</beacon>") == 0)
		  break;

		if (strcmp(name, "cycle-size") == 0) {
		  int v;
		  if (config_parse_interval(param1, &v)) {
		    // Error
		    has_fault = 1;
		    continue;
		  }
		  bset->beacon_cycle_size = (float)v;
		  if (debug)
		    printf("Beacon cycle size: %.2f\n",
			   bset->beacon_cycle_size/60.0);
		  continue;
		}

		if (strcmp(name, "beacon") == 0) {
		  beacon_set(cf, param1, str, beaconmode, bset);

		} else if (strcmp(name, "beaconmode") == 0) {
		  if (strcasecmp(param1, "both") == 0) {
		    beaconmode = 0;

		  } else if (strcasecmp(param1,"radio") == 0) {
		    beaconmode = 1;

		  } else if (strcasecmp(param1,"aprsis") == 0) {
		    beaconmode = -1;

		  } else {
		    printf("%s:%d ERROR: Unknown beaconmode parameter keyword: '%s'\n",
			   cf->name, cf->linenum, param1);
		    has_fault = 1;
		  }

		} else {
		  printf("%s:%d ERROR: Unknown <beacon> block config keyword: '%s'\n",
			 cf->name, cf->linenum, name);
		  has_fault = 1;
		  continue;
		}
	}
        if (has_fault) {
          // discard it..
          free_beaconset(bset);
        } else {
          // save it..
          ++bsets_count;
          bsets = realloc( bsets,sizeof(*bsets)*bsets_count );
          bsets[bsets_count-1] = bset;

          if (debug > 0) {
            printf("<beacon> set %d defined with %d entries\n",
                   bsets_count, bset->beacon_msgs_count);
          }
        }

	return has_fault;
}

static void fix_beacon_time(char *txt, int txtlen)
{
	int hour, min, sec;
	char hms[8];

	sec = now.tv_sec % (3600*24); // UNIX time is UTC -> no need to play with fancy timezone conversions and summer times...
	hour = sec / 3600;
	min  = (sec / 60) % 60;
	sec  = sec % 60;
	sprintf(hms, "%02d%02d%02dh", hour, min, sec);

	txt += 2; txtlen -= 2; // Skip Control+PID

	if (*txt == ';' && txtlen >= 36) { // Object

		// ;434.775-B*111111z6044.06N/02612.79Er
		memcpy( txt+11, hms, 7 ); // Overwrite with new time
	} else if ((*txt == '/' || *txt == '@') && txtlen >= 27) { // Position with timestamp
		memcpy( txt+1, hms, 7 ); // Overwrite with new time
	}
}


static char *msg_read_file(const char *filename, char *buf, int buflen)
{
	FILE *fp = fopen(filename,"r");
	if (!fp) return NULL;
	if (fgets(buf, buflen, fp)) {
		char *p = strchr(buf, '\n');
		if (p) *p = 0;
	} else {
		*buf = 0;
	}
	fclose(fp);
	if (*buf == 0) return NULL;
	return buf;
}

static void beacon_resettimer(void *arg)
{
	const struct beaconset *bset = (struct beaconset *)arg;
	float beacon_increment;
	int   i;
	time_t t = tick.tv_sec;

	srand((long)t);
        beacon_increment = (bset->beacon_cycle_size / bset->beacon_msgs_count);

        if (debug)
        	printf("beacons cycle: %.2f minutes, increment: %.2f minutes\n",
                       bset->beacon_cycle_size/60.0, beacon_increment/60.0);
        
        for (i = 0; i < bset->beacon_msgs_count; ++i) {
        	int r = rand() % 1024;
                int interval = (int)(beacon_increment - 0.2*beacon_increment * (r*0.001));
                if (interval < 3) interval = 3; // Minimum interval: 3 seconds
                t += interval;
                if (debug)
                	printf("beacons offset: %.2f minutes\n", (t-tick.tv_sec)/60.0);
                bset->beacon_msgs[i]->nexttime = t;
        }
}

static void msg_exec_read(struct beaconset *bset)
{
	int rc;
        int space = bset->exec_buf_space - bset->exec_buf_length;
        if (debug) printf("msg_exec_read\n");

        if (space < 1) {
        	space += 256;
                bset->exec_buf_space += 256;
                bset->exec_buf = realloc(bset->exec_buf, bset->exec_buf_space);
        }
        
        while ((rc = read(bset->exec_fd, bset->exec_buf + bset->exec_buf_length, space)) > 0) {
		char *p;
        	bset->exec_buf_length += rc;
                space -= rc;
                p = memrchr(bset->exec_buf, '\n', bset->exec_buf_length);
                if (p) {
                  if (debug) printf("found newline in exec read data\n");
                  *p = 0;
                  bset->exec_buf_length = p - bset->exec_buf;
                  struct beaconmsg *bm = bset->exec_bm;
                  if (bset->exec_buf_length > 2) {
                    // Run that beacon!
                    // Point it to read buffer
                    bm->msg = bset->exec_buf;
                    if (debug) printf(".. calling beacon_it() on buffer: %s\n", bm->msg+2);
                    beacon_it(bset, bm);
                  } else {
                    if (debug) printf(".. nothing read from exec pipe\n");
                  }
                  // erase the read buffer pointer
                  bm->msg = NULL;
                  // restore the nexttime
                  bset->beacon_nexttime.tv_sec = bm->nexttime;
                  close(bset->exec_fd);
                  bset->exec_fd = -1;
                  //bset->exec_pid = 0; 
                  return;
                }
                if (debug) printf("no newline in exec read data\n");
                if (space < 1) {
                  aprxlog("BEACON EXEC output overflowed read buffer.");
                  rc = 0; // simulate as if..  and kill it.
                  break;
                }
        }
        if (rc == 0) { // EOF read
		char *p;
		if (debug) printf("Seen EOF on exec-read\n");
                p = memrchr(bset->exec_buf, '\n', bset->exec_buf_length);
                if (p) {
                  *p = 0;
                  bset->exec_buf_length = p - bset->exec_buf;
                  struct beaconmsg *bm = bset->exec_bm;
                  if (bset->exec_buf_length > 2) {
                    // Run that beacon!
                    // Point it to read buffer
                    bm->msg = bset->exec_buf;
                    if (debug) printf(".. calling beacon_it() on buffer: %s\n", bm->msg+2);
                    beacon_it(bset, bm);
                  } else {
                    if (debug) printf(".. nothing read from exec pipe\n");
                  }
                  // erase the read buffer pointer
                  bm->msg = NULL;
                  // restore the nexttime
                  bset->beacon_nexttime.tv_sec = bm->nexttime;
                } else {
                  aprxlog("BEACON EXEC abnormal close.");
                }
                close(bset->exec_fd);
                bset->exec_fd = -1;
                //bset->exec_pid = 0; 
        }
}

static int msg_exec_file(const char *filename, int timeout, struct beaconset *bset)
{
	int p[2];
        int pid;
        int dev_null;
	if (pipe(p)) {
		return 0;
	}

        pid = fork();
	if (pid < 0) {
		close(p[0]);
		close(p[1]);
		return 0;
	}
        if (pid == 0) { //child

        	if (debug) fprintf(stderr,"execing child pid %d, file: %s\n", getpid(), filename);

		close(p[0]);
                if (p[1] != 1) {
                  dup2(p[1], 1);
                  close(p[1]);
                }
                dev_null = open("/dev/null", O_WRONLY);
		if (debug && dev_null < 0)
                  fprintf(stderr,"child process: Failed to open file: /den/null\n");
                if (dev_null >= 0) {
                  if (dev_null != 2) {
                    dup2(dev_null, 2);
                    close(dev_null);
                  }
                  dup2(2, 0);
                }

		//FIXME: change second parameter
		execl(filename, "aprx", NULL);
		if (debug)
                  fprintf(stderr,"child process: Failed to execute: %s\n", filename);
		exit(255);

	}

        // parent

        bset->exec_deadline = tick.tv_sec + timeout;
        bset->exec_pid = pid;
        bset->exec_fd  = p[0];
        
        bset->beacon_nexttime.tv_sec = bset->exec_deadline;
        
        close(p[1]);

        return 1;
}

//        int val;
//        waitpid(pid, &val, 0);
//        if (WIFEXITED(val) && WEXITSTATUS(val) == 0) {
//          return buf;
//        }

static void beacon_now(struct beaconset *bset)
{
	struct beaconmsg *bm;

        if (bset->exec_pid > 0) {
          if (debug) printf("beacon_now - still an exec under way.\n");
          // Wait 3 seconds before retrying.
          bset->beacon_nexttime.tv_sec += 3;
          return;
        }

	if (bset->beacon_msgs_cursor >= bset->beacon_msgs_count) // Last done..
		bset->beacon_msgs_cursor = 0;

	if (bset->beacon_msgs_cursor == 0) {
        	beacon_resettimer(bset);
	}

	/* --- now the business of sending ... */

        //if (debug) printf("beacon_now idx=%d\n", bset->beacon_msgs_cursor );
	bm = bset->beacon_msgs[bset->beacon_msgs_cursor++];

	bset->beacon_nexttime.tv_sec = bm->nexttime;
	bset->beacon_nexttime.tv_usec = 0;

        beacon_it(bset, bm);
}

static void beacon_it(struct beaconset *bset, struct beaconmsg *bm)
{
	int  destlen;
	int  txtlen, msglen;
	int  i;
	char const *txt;
	char *msg;

	if (debug)
	  printf("BEACON: idx=%d, nexttime= +%d sec\n",
		 bset->beacon_msgs_cursor-1, (int)(bset->beacon_nexttime.tv_sec - tick.tv_sec));

	destlen = strlen(bm->dest) + ((bm->via != NULL) ? strlen(bm->via): 0) +2;

	if (bm->filename != NULL) {
		msg = alloca(256);  // This is a load-and-discard allocation
		txt = msg+2;
		msg[0] = 0x03;
		msg[1] = 0xF0;
		if (!msg_read_file(bm->filename, msg+2, 256-2)) {
			// Failed loading
			if (debug)
			  printf("BEACON ERROR: Failed to load anything from file %s\n",bm->filename);
			syslog(LOG_ERR, "Failed to load anything from beacon file %s", bm->filename);
			return;
		}
        } else if (bm->msg != NULL) {
		msg     = (char*)bm->msg;
		txt     = bm->msg+2; // Skip Control+PID bytes
	} else if (bm->execfile != NULL) {
		bset->exec_buf = realloc(bset->exec_buf, 256);
                bset->exec_buf[0] = 0x03;
                bset->exec_buf[1] = 0xF0;
                bset->exec_buf_length = 2;
                bset->exec_buf_space = 256;
                bset->exec_bm = bm;
		if (!msg_exec_file(bm->execfile, bm->timeout, bset)) {
			if (debug)
			  printf("BEACON ERROR: Failed to exec file %s\n",bm->execfile);
			syslog(LOG_ERR, "Failed to exec file %s", bm->execfile);
			return;
		}
                return; // spawning done, successfull or not..
	} else {
          if (debug>1) printf("Nothing to beacon now.\n");
          return;
        }

	txtlen  = strlen(txt);
	msglen  = txtlen+2; // this includes the control+pid bytes

	/* _NO_ ending CRLF, the APRSIS subsystem adds it. */

	/* Send those (rf)beacons.. (a noop if interface == NULL) */
	if (bm->interface != NULL) {
		const char *callsign = bm->interface->callsign;
		const char *src = (bm->src != NULL) ? bm->src : callsign;
		int   len  = destlen + 12 + strlen(src); // destlen contains bm->via plus room for ",TCPIP*"
		char *destbuf = alloca(len);

                // Now it is time to beacon something, lets make sure
                // the source callsign is not APRSIS !
                if (strcmp(src,"APRSIS") == 0) {
                  if (debug)
                    printf("CONFIGURATION ERROR: Beacon with source callsign APRSIS. Skipped!\n");
                  return;
                }

		if (bm->timefix)
		  fix_beacon_time(msg, msglen);

#ifndef DISABLE_IGATE
		if (bm->beaconmode <= 0) {

                  if (bm->via != NULL)
                    sprintf(destbuf,"%s>%s,%s,TCPIP*", src, bm->dest, bm->via);
                  else
                    sprintf(destbuf,"%s>%s,TCPIP*", src, bm->dest);

                  if (debug) {
                    printf("%ld\tNow beaconing to APRSIS %s '%s' -> '%s',",
                           tick.tv_sec, callsign, destbuf, txt);
                    printf(" next beacon in %.2f minutes\n",
                           ((bset->beacon_nexttime.tv_sec - tick.tv_sec)/60.0));
                  }

		  // Send them all also as netbeacons..
		  aprsis_queue(destbuf, strlen(destbuf),
			       qTYPE_LOCALGEN,
			       aprsis_login, txt, txtlen);
		}
#endif

		if (bm->beaconmode >= 0 && bm->interface->tx_ok) {
		  // And to interfaces
                  char *dp = destbuf; // destbuf collects ONLY the VIA data

		  if (strcmp(src, callsign) != 0) {
		    if (bm->via != NULL)
		      dp += sprintf( dp, "%s*,%s", callsign, bm->via );
		    else
		      dp += sprintf( dp, "%s*", callsign );
		  } else {
		    if (bm->via != NULL)
		      dp += sprintf( dp, "%s", bm->via );
                    else
                      *dp = 0;
		  }
                  
                  if (debug) {
                    printf("%ld\tNow beaconing to interface[1] %s(%s) '%s' -> '%s',",
                           tick.tv_sec, callsign, src, destbuf, txt);
                    printf(" next beacon in %.2f minutes\n",
                           ((bset->beacon_nexttime.tv_sec - tick.tv_sec)/60.0));
                  }

		  interface_transmit_beacon(bm->interface,
					    src,
					    bm->dest,
					    destbuf, // via data
					    msg, msglen);
		}
	} 
	else {
	    for ( i = 0; i < all_interfaces_count; ++i ) {
		const struct aprx_interface *aif = all_interfaces[i];
		const char *callsign = aif->callsign;
		const char *src = (bm->src != NULL) ? bm->src : callsign;
		int len = destlen + 12 + (src != NULL ? strlen(src) : 0); // destlen contains bm->via, plus room for ",TCPIP*"
		char *destbuf = alloca(len);

                if (debug>1)
                  printf("Beacon: aif=%p callsign='%s' src='%s' bm->dest='%s' bm->via='%s'\n",
                         aif, callsign, src, bm->dest, bm->via);

		if (!interface_is_beaconable(aif)) {
                  if (debug>1)
                    printf("Not a beaconable interface, skipping\n");
		  continue; // it is not a beaconable interface
                }

		if (callsign == NULL) {
		  // Probably KISS master interface, and subIF 0 has no definition.
                  if (debug>1)
                    printf("No callsign on interface interface, skipping\n");
		  continue;
		}

		if (aif->iftype == IFTYPE_APRSIS) {
		  // If we have no radio interfaces, we may still 
		  // want to do beacons to APRSIS.  Ignore the
		  // builtin APRSIS interface if there are more
		  // interfaces available!
		  if (all_interfaces_count > 1) {
                    if (debug>2)
                      printf("Beaconing to APRSIS interface ignored in presence of other interfaces. Skipping.\n");
		    continue;  // Ignore the builtin APRSIS interface
                  }
		}

                // Now it is time to beacon something, lets make sure
                // the source callsign is not APRSIS !
                if (strcmp(src,"APRSIS") == 0) {
                  if (debug)
                    printf("CONFIGURATION ERROR: Beaconing with source callsign APRSIS!  Skipping.\n");
                  continue;
                }

		
		if (bm->timefix)
		  fix_beacon_time((char*)msg, msglen);
		
#ifndef DISABLE_IGATE
		if (bm->beaconmode <= 0) {
		  // Send them all also as netbeacons..

                  if (bm->via != NULL)
                    sprintf(destbuf,"%s>%s,%s,TCPIP*", src, bm->dest, bm->via);
                  else
                    sprintf(destbuf,"%s>%s,TCPIP*", src, bm->dest);
                  
                  if (debug) {
                    printf("%ld\tNow beaconing to APRSIS %s(%s) '%s' -> '%s',",
                           tick.tv_sec, callsign, src, destbuf, txt);
                    printf(" next beacon in %.2f minutes\n",
                           ((bset->beacon_nexttime.tv_sec - tick.tv_sec)/60.0));
                  }

		  aprsis_queue(destbuf, strlen(destbuf),
			       qTYPE_LOCALGEN,
			       aprsis_login, txt, txtlen);
		}
#endif

		if (bm->beaconmode >= 0 && aif->tx_ok) {
		  // And to transmit-capable interfaces
                  char *dp = destbuf; // destbuf collects ONLY the VIA data

		  // The 'destbuf' has a plenty of room
		  if (strcmp(src, callsign) != 0) {
		    if (bm->via != NULL)
		      dp += sprintf( dp, "%s*,%s", callsign, bm->via );
		    else
		      dp += sprintf( dp, "%s*", callsign );
		  } else {
		    if (bm->via != NULL)
		      dp += sprintf( dp, "%s", bm->via );
                    else
                      *dp = 0;
		  }
		  
                  if (debug) {
                    printf("%ld\tNow beaconing to interface[2] %s(%s) '%s' -> '%s',",
                           tick.tv_sec, callsign, src, destbuf, txt);
                    printf(" next beacon in %.2f minutes\n",
                           ((bset->beacon_nexttime.tv_sec - tick.tv_sec)/60.0));
                  }

		  interface_transmit_beacon(aif,
					    src,
					    bm->dest,
					    destbuf, // via data
					    msg, msglen);
		}
	    }
	}
}

int beacon_prepoll(struct aprxpolls *app)
{
	int i;
#ifndef DISABLE_IGATE
	if (!aprsis_login)
		return 0;	/* No mycall !  hoh... */
#endif
        for (i = 0; i < bsets_count; ++i) {
        	struct beaconset *bset = bsets[i];
                if (bset->beacon_msgs == NULL) continue; // nothing here

                if (time_reset) {
                	// master time pickup noticed time back-tracking
                	beacon_resettimer(bset);
                }

                if (tv_timercmp(&bset->beacon_nexttime, &app->next_timeout) < 0)
                	app->next_timeout = bset->beacon_nexttime;

                if (bset->exec_pid != 0 && bset->exec_fd >= 0) {
                	struct pollfd *pfd;
                        // FD is open, lets mark it for poll read..
                        pfd = aprxpolls_new(app);
                        pfd->fd = bset->exec_fd;
                        pfd->events = POLLIN | POLLPRI;
                        pfd->revents = 0;
                }
        }

	return 0;		/* No poll descriptors, only time.. */
}


int beacon_postpoll(struct aprxpolls *app)
{
	int idx, i;
	//struct serialport *S;
	struct pollfd *P;
#ifndef DISABLE_IGATE
	if (!aprsis_login)
		return 0;	/* No mycall !  hoh... */
#endif
        for (i = 0; i < bsets_count; ++i) {
        	struct beaconset *bset = bsets[i];
                if (bset->exec_pid > 0 && bset->exec_deadline < tick.tv_sec) {
			// Waited too long, discard it.
                	//printf("killing subprogram pid=%d mypid=%d\n", bset->exec_pid, getpid());
                        if (debug) printf("Killing overdue beacon exec subprogram pid %d\n", bset->exec_pid);
                	kill(bset->exec_pid, SIGKILL);
                        bset->exec_pid = - bset->exec_pid;
                }
                for (idx = 0, P = app->polls; idx < app->pollcount; ++idx, ++P) {
                	if (bset->exec_fd == P->fd) {
                          if (debug>1) printf("revents of exec_fd = 0x%x\n", P->revents);
                          if (P->revents & (POLLIN | POLLPRI | POLLHUP)) {
                            msg_exec_read(bset);
                          }
                        }
                }
                if (bset->beacon_msgs == NULL) continue; // nothing..
                if (tv_timercmp(&bset->beacon_nexttime, &tick) > 0) continue; // not yet

                beacon_now(bset);
        }

        if (debug>1) printf("beacon_postpoll()\n");


	return 0;
}

void beacon_childexit(int pid)
{
	int i;
        for (i = 0; i < bsets_count; ++i) {
        	struct beaconset *bset = bsets[i];
                if (pid == bset->exec_pid) {
                	bset->exec_pid = -pid;
                        if (debug) {
                          // Avoid stdio FILE* interlocks within signal handler
                          char buf[64];
                          sprintf(buf, "matched child exit, pid=%d\n", pid);
                          write(1, buf, strlen(buf));
                        }
                        break;
                }
        }
}
