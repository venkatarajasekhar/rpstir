#include "main.h"

int
main(int argc, char *argv[])
{

  /* tflag = tcp, uflag = udp, nflag = do nothing, just print 
     what you would have done, {w,e,i}flag = {warning,error,
     information} flags for what will be sent, ch is for getopt */

  int tflag, uflag, nflag, fflag, ch;
  int portno;
  unsigned int retlen;
  FILE *fp;
  char *sendStr;
  struct write_port wport;
  char holding[PATH_MAX];
  char flags;  /* our warning flags bit fields */

  tflag = uflag = nflag = fflag = ch = 0;
  portno = retlen = 0;
  flags = 0;

  memset((char *)&wport, '\0', sizeof(struct write_port));

  while ((ch = getopt(argc, argv, "t:u:f:nweih")) != -1) {
    switch (ch) {
      case 't':  /* TCP flag */
        tflag = 1;
        portno = atoi(optarg);
        break;
      case 'u':  /* UDP flag */
        uflag = 1;
        portno = atoi(optarg);
        break;
      case 'n': /* do nothing flag - print what messages would have been
                   sent */
        nflag = 1;
        break;
      case 'w': /* create warning message(s) */
        flags = flags | WARNING_FLAG;
        break;
      case 'e': /* create error message(s) */
        flags = flags | ERROR_FLAG;
        break;
      case 'i': /* create information message(s) */
        flags = flags | INFO_FLAG;
        break;
      case 'f': /* log file to read and parse */
        fflag = 1;
        fp = fopen(optarg, "r");
        if (!fp) {
          fprintf(stderr, "failed to open %s\n", optarg);
          exit(1);
        }
        break;
      case 'h': /* help */
      default:
        usage(argv[0]);
        break;
    }
  }

  /* test for necessary flags */
  if (!fflag) {
    fprintf(stderr, "please specify rsync logfile with -f. Or -h for help\n");
    exit(1);
  }

  /* test for conflicting flags here... */
  if (tflag && uflag) {
    fprintf(stderr, "choose either tcp or udp, not both. or -h for help\n");
    exit(1);
  }

  if (!tflag && !uflag && !nflag) { /* if nflag then we don't care */
    fprintf(stderr, "must choose tcp or udp, or specify -n. -h for help\n");
    exit(1);
  }

  /* setup sockets... */
  if (!nflag) {
    if (tflag) {
      if (tcpsocket(&wport, portno) != TRUE) {
        fprintf(stderr, "tcpsocket failed...\n");
        exit(-1);
      }
    } else if (uflag) {
      if (udpsocket(&wport, portno) != TRUE) {
        fprintf(stderr, "udpsocket failed...\n");
        exit(-1);
      }
    }     
  } else {
    wport.out_desc = STDOUT_FILENO;
    wport.protocol = LOCAL;
  }

  /* set the global pointer to the wport struct here - don't
     know if this will cause a fault or not. Can't remember.
     Doing this to be able to communicate with the server
     through the descriptor after a sigint or other signal
     has been caught. */
  global_wport = &wport;

  if (setup_sig_catchers() != TRUE) {
    fprintf(stderr, "failed to setup signal catchers... bailing.\n");
    exit(FALSE);
  }

  /****************************************************/
  /* Make the Start String                            */
  /* send the Start String                            */
  /* free it                                          */
  /****************************************************/
  sendStr = makeStartStr(&retlen);
  if (!sendStr) {
    fprintf(stderr, "failed to make Start String... bailing...\n");
    exit(1);
  }

  outputMsg(&wport, sendStr, retlen);
  retlen = 0;
  free(sendStr);

  /****************************************************/
  /* do the main parsing and sending of the file loop */
  /****************************************************/
  while (fgets(holding, PATH_MAX, fp) != NULL) {
    sendStr = getMessageFromString(holding, (unsigned int)strlen(holding), 
                                   &retlen, flags);
    if (sendStr) {
      outputMsg(&wport, sendStr, retlen);
      retlen = 0;
      free(sendStr);
    }
    memset(holding, '\0', sizeof(holding));
  }

  /****************************************************/
  /* Make the End String                              */
  /* send the End String                              */
  /* free it                                          */
  /****************************************************/
  sendStr = makeEndStr(&retlen);
  if (!sendStr) {
    fprintf(stderr, "failed to make End String... bailing...\n");
    exit(1);
  }
  outputMsg(&wport, sendStr, retlen);
  free(sendStr);
  
  /* close descriptors etc. */
  if (wport.protocol != LOCAL) {
    close(wport.out_desc);
  }
  return(0);
}

