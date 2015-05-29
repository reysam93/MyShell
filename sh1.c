//practica sh1.c

#include <stdio.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <dirent.h>


//CONSTS
enum {maxcmds=30, maxargs=30, sizeofbuf=8*1024};

struct cmdtype{
	char* argv[maxargs];
	int fdin;
	int fdout;
	int nextfdin;
};

typedef struct cmdtype cmdt;

struct linetype{
	char line[sizeofbuf];
	int bg;
	int heredoc;
	int fdspercent[2];
	cmdt cmds[maxcmds];
	int cmdc;
	char* stdin;
	char* stdout;
};

typedef struct linetype linet;


int
runcmdline (linet line);


int 
readcmd (linet *l, char *cmd){
	char* arg, *paux;
	int i, cont;

	cont = (*l).cmdc;
	for (i=0;;cmd=NULL, i++){
		arg=strtok_r(cmd, " \n\t\r", &paux);
		if (arg==NULL){
			(*l).cmds[cont].argv[i]=NULL;
			break;
		}
		switch (*arg){
		case '<':
			if (*(arg+1)==0){
				(*l).stdin = strtok_r(cmd, " \n\t\r", &paux);
			}else{
				(*l).stdin = arg +1;
			}
			i--;
			break;
		case '>':
			if (*(arg+1)==0){
				(*l).stdout = strtok_r(cmd, " \n\t\r", &paux);
			}else{
				(*l).stdout = arg+1;	
			}
			i--;
			break;
		default:
			if (i>=maxargs-1){
				fprintf (stderr, "To many arguments. Maxargs = %d.\n", maxargs-1);
				return -1;
			}
			(*l).cmds[cont].argv[i]=arg;
		}
	}
	if ((*l).heredoc == 0 || cont != 0){
		(*l).cmds[cont].fdin = 0;
	}
	(*l).cmds[cont].fdout = 0;
	(*l).cmds[cont].nextfdin = 0;
	return 0;
}

int
processequal (char* l, char* v2){
	char* v1, *paux;
	char *v1aux, *v2aux;

	v1=strtok_r(l, " \n\t\r=", &paux);
	v2=strtok_r(NULL, " \n\t\r=", &paux);
	v1aux = strdup(v1);
	if (v1aux == NULL){
		warn("strdup %s", v1);
		return -1;
	}
	v2aux = strdup(v2);
	if (v2aux == NULL){
		warn("strdup %s", v2);
		return -1;
	}
	if (snprintf (l, sizeofbuf, "equal %s %s", v1aux, v2aux)<0){
		warn("processequal snprintf");
		return -1;
	}
	free (v1aux);
	free (v2aux);
	return 0;
}

int
processdolar (char* line){
	char *pdolar, *paux, *ppart2, *pdvalue;
	int lenpar1, lendolar, lenline;

	for(;;){
		pdolar = strchr(line, '$');
		if (pdolar==NULL){
			break;
		}else{
			lenline=strlen(line);
			*pdolar = '\0';
			pdolar++;
			if (*pdolar=='\n'){
				fprintf(stderr, "Error. Must put something behind $\n");
				return -1;
			}
			pdolar = strtok_r(pdolar, " \n\t\r", &paux);
			lenpar1 = strlen(line)+1;
			lendolar = strlen(pdolar)+1;
			ppart2 = pdolar + lendolar;
			pdvalue = getenv(pdolar);
			if (pdvalue==NULL){
				fprintf (stderr, "error: var %s does not exist\n", pdolar);
				return -1;
			}
			if ((lenline - lendolar + strlen(pdvalue)) >= sizeofbuf-1){
				fprintf (stderr, "Max size of cmd line = %d characters\n", sizeofbuf-1);
				return -1;
			}
			ppart2 = strdup(ppart2);
			if (ppart2 == NULL){
				warn("strdup %s", ppart2);
				return -1;
			}
			strcpy((line+lenpar1-1), pdvalue);
			lenline=strlen(line);
			*(line+lenline)=' ';
			strcpy(line+lenline+1, ppart2);	
			free(ppart2);
		}
	}
	return 0;
}

int
processpercent (linet *l, char* v2){
	char *v1, *paux, *vaux1, *vaux2;

	if (pipe((*l).fdspercent)<0){
		warn("pipe");
		return -1;
	}
	v1=strtok_r((*l).line, "%", &paux);
	if (*v1=='\n'){
		fprintf(stderr, "Error. Format is: environment variable percent cmds\n");
		return -1;
	}
	v2=strtok_r(NULL, "%", &paux);
	if (v2==NULL || *v2=='\n'){
		fprintf(stderr, "Error. Format is: environment variable percent cmds\n");
		return -1;
	}
	vaux1 = strdup(v1);
	if (vaux1 == NULL){
		warn("strdup %s", v1);
		return -1;
	}
	vaux2 = strdup(v2);
	if (vaux2 == NULL){
		warn("strdup %s", v2);
		return -1;
	}
	if (snprintf ((*l).line, sizeofbuf, "percent %s %s", vaux1, vaux2)<0){
		warn("processequal snprintf");
		return -1;
	}
	return 0;
}

int
processheredocument (linet *l, char* here){
	char buf[8*1024];
	int fds[2];

	(*l).heredoc = 1;
	*here = '\0';
	if (pipe(fds)<0){
		warn("pipe");
		return -1;
	}
	(*l).cmds[0].fdin = fds[0];
	for (;;){
		if (fgets(buf, sizeof(buf), stdin)==NULL){
			return 0;
		}
		if (buf[0]==']'){
			break;
		}
		if (write(fds[1], buf, strlen(buf)) != strlen(buf)){
			warn("write %s", buf);
			return -1;
		}
	}
	if (close(fds[1])<0){
		warn("close %d", fds[1]);
		return -1;
	}
	return 0;
}

int
processcmdline (linet* l){
	char *symbol;

	if (processdolar((*l).line)<0){
		return -1;
	}
	symbol = strchr ((*l).line, '=');
	if (symbol != NULL){
		if (processequal ((*l).line, symbol)<0){
			return -1;
		}
	}
	symbol = strchr((*l).line, '%');
	if (symbol != NULL){
		if (processpercent(l, symbol)<0){
			return -1;
		}
	}
	symbol = strchr ((*l).line, '&');
	if (symbol != NULL){
		(*l).bg = 1;
		*symbol = ' ';
	}
	symbol = strchr((*l).line, '[');
	if (symbol != NULL){
		if ((*l).bg == 1 || strchr((*l).line, '<') != NULL || strchr((*l).line, '>') != NULL){
		fprintf (stderr, "Can't use '[' and '&' or '>' or '<'\n");
		return -1;
		}
		processheredocument(l, symbol);
	}
	return 0;
}

int
readline (linet* line){
	char *paux, *pline, *cmd;	

	(*line).cmdc = 0;
	(*line).stdin = NULL;
	(*line).stdout = NULL;
	(*line).bg = 0;
	(*line).heredoc = 0;
	(*line).fdspercent[0] = 0;
	(*line).fdspercent[1] = 0;
	if (fgets((*line).line, sizeofbuf, stdin)==NULL){
		return 1;
	}
	if(strlen(line->line) >= sizeofbuf-1){
		fprintf(stderr, "Command line is too long\n");
		return -1;
	}
	if (*(*line).line=='\n'){
		return -1;
	}
	if (processcmdline(line)<0){
		return -1;
	}
	pline = (*line).line;
	for (;;pline=NULL, (*line).cmdc++){
		cmd =strtok_r (pline, "|", &paux);		
		if (cmd==NULL){
			break;
		}
		if ((*line).cmdc >= maxcmds){
			fprintf (stderr, "To many commands. Max cmds = %d.\n", maxcmds);
			return -1;
		}
		if (readcmd (line, cmd)<0){		
			return -1;
		}
	}
	return 0;
}

int
setresult (int value){

	if (value == 0){
		if (setenv("result", "0", 1)<0){
			warn("setenv result");
			return -1;
		}
	}else{
		if (setenv("result", "1",1)<0){
			warn("setenv result");
			return -1;
		}
	}
	return 0;
}

int
changedir (char* argv[]){
	char* path;

	if (argv[1]==NULL){
		path = getenv("HOME");
	}else{
		path = argv[1];
	}
	if (chdir (path)<0){
		warn ("chdir %s", path);
		return -1;
	}
	return 0;
}

int
equal (char* var1, char* var2){

	if(setenv(var1, var2, 1)<0){
		warn("setenv %s=%s", var1, var2);
		return -1;
	}
	return 0;
}

int
readresultofcmdline(char buf[], int fd, int bufsize){
	int nrt, nr;

	for (nrt=0;;nrt=nrt+nr){		
		nr = read(fd, buf+nrt, sizeof(buf));
		if (nr <0){
			warn("read from %d", fd);
			return -1;
		}
		if (nr==0){
			break;
		}
		if (nrt>bufsize){
			fprintf(stderr, "Size of the environment var is too long\n");
			return -1;
		}
	}
	return 0;
}

int
emptybuf (char buf[], int size){
	int fdaux;

	fdaux=open("/dev/zero", O_RDONLY);
	if(fdaux<0){
		warn("open /dev/zero");
		return -1;
	}
	if(read (fdaux, buf, size)<0){
		warn("read /dev/zoer");
		return -1;
	}
	if (close(fdaux)<0){
		return -1;
	}
	return 0;
}

int
percent (linet l, int i){
	int j;
	char* var, buf[4*1024];

	var=strdup(l.cmds[i].argv[1]);
	if (emptybuf(buf, sizeof(buf))<0){
		return -1;
	}
	if (var == NULL){
		warn("strdup %s", l.cmds[i].argv[1]);
		return -1;
	}
	for(j=2;l.cmds[i].argv[j]!=NULL;j++){				
		l.cmds[i].argv[j-2]=l.cmds[i].argv[j];
	}
	l.cmds[i].argv[j-2] = NULL;
	if (runcmdline (l)<0){
		return -1;
	}
	if (readresultofcmdline(buf, l.fdspercent[0], sizeof(buf))<0){
		return -1;
	}
	if (equal(var, buf)<0){
		return -1;
	}
	if (close(l.fdspercent[0])<0){
		warn("close %d", l.fdspercent[0]);
		return -1;
	}
	free (var);
	return 0;
}

int
showresult (linet l, int i){

	l.cmds[i].argv[0]= "echo";
	l.cmds[i].argv[1]= getenv("result");
	if (l.cmds[i].argv[1]==NULL){
		fprintf(stderr, "var result is not set yet\n");
		return -1;
	}
	l.cmds[i].argv[2]= NULL;
	if (runcmdline (l)<0){
		return -1;
	}
	return 0;
}

int
ifok (linet l, int i){
	char *result;
	int j;

	result = getenv("result");
	if (result == NULL){
		fprintf(stderr, "var result is not set yet\n");
		return -1;
	}
	if(strcmp(result, "0")==0){
		for (j=1; l.cmds[i].argv[j]!=NULL; j++){
			l.cmds[i].argv[j-1]=l.cmds[i].argv[j];
		}
		l.cmds[i].argv[j-1]= NULL;
		if (runcmdline(l)<0){
			return -1;
		}
	}
	return 0;
}

int
ifnot (linet l, int i){
	char *result;
	int j;

	result = getenv("result");
	if (result == NULL){
		fprintf(stderr, "var result is not set yet\n");
		return -1;
	}
	if(strcmp(result, "1")==0){
		for (j=1; l.cmds[i].argv[j]!=NULL; j++){
			l.cmds[i].argv[j-1]=l.cmds[i].argv[j];
		}
		l.cmds[i].argv[j-1]= NULL;
		if (runcmdline(l)<0){
			return -1;
		}
	}
	return 0;
}

int
isbuiltin (linet l, int i){
	int status;

	if (strcmp(l.cmds[i].argv[0],"cd")==0){
		status = changedir (l.cmds[i].argv);
	}else if (strcmp(l.cmds[i].argv[0], "equal")==0){
		status = equal(l.cmds[i].argv[1], l.cmds[i].argv[2]);
	}else if (strcmp(l.cmds[i].argv[0], "percent")==0){
		status = percent(l, i);
	}else if (strcmp(l.cmds[i].argv[0], "result")==0){
		status = showresult(l, i);
	}else if (strcmp(l.cmds[i].argv[0], "ifok")==0){
		status = ifok(l,i);	
	}else if (strcmp(l.cmds[i].argv[0], "ifnot")==0){
		status = ifnot(l,i);
	}else{
		return 1;
	}
	setresult(status);
	return status;
}

int
searchpath (char* cmd, char** path){
	char* pathaux, *paux;
	char buf[1024], newpath[1024];

	pathaux = strncpy (buf, getenv ("PATH"), sizeof(buf));  
	for (;;pathaux=NULL){
		*path = strtok_r (pathaux, ":", &paux);
		if (*path == NULL){
			break;
		}
		if (snprintf (newpath, sizeof(newpath), "%s/%s", *path, cmd)<0){
			warn("search path snprintf");
			return -1;
		}
		if (access(newpath, X_OK)==0){
			*path = newpath;
			return 0;
		}
	}
	return -1;
}

int
inoutredirect (cmdt cmd){

	if (cmd.fdin != 0){
		if(dup2 (cmd.fdin, 0)<0){
			warn ("dup2 fdin: %d", cmd.fdin);
			return -1;
		}
		if (close (cmd.fdin)<0){
			warn ("close fdin: %d", cmd.fdin);
			return -1;
		}
	}
	if (cmd.fdout != 0){
		if (dup2(cmd.fdout, 1)<0){
			warn ("dup2 fdout: %d", cmd.fdout);
			return -1;
		}
		if (close(cmd.fdout)<0){
			warn ("close fdout: %d", cmd.fdout);
			return -1;
		}
	}
	if (cmd.nextfdin != 0){
		if (close(cmd.nextfdin)<0){
			warn ("close nextfdin: %d", cmd.nextfdin);
			return -1;
		}
	}
	return 0;
}

int
closefds (cmdt cmd){

	if (cmd.fdin != 0){
		if (close(cmd.fdin)<0){
			warn ("PADRE.Close fdin: %d", cmd.fdin);
			return -1;
		}
	}
	if (cmd.fdout != 0){
		if (close(cmd.fdout)<0){
			warn ("PADRE.Close fdout: %d", cmd.fdout);
			return -1;
		}
	}
	return 0;
}

int
execcmd (cmdt cmd, char* path, int *pid){

	*pid =fork ();
	switch (*pid){
	case -1:
		warn ("fork");
		return -1;
	case 0:
		if (inoutredirect (cmd)<0){
			exit(1);
		}
		execv (path, cmd.argv);
		err (1, "exec %s", cmd.argv[0]);
	default:
		if (closefds (cmd)<0){
			return -1;
		}
		return 0;
	}
}

int
runcmd (linet l, int i, int *pid){
	char* path;

	path = NULL;
	switch (isbuiltin (l, i)){			
	case 0:
		return 1;
	case -1:
		return -2;
	}
	if (access (l.cmds[i].argv[0], X_OK)==0){
		path = l.cmds[i].argv[0];
	}
	if ((path == NULL) && (searchpath(l.cmds[i].argv[0], &path)!=0)){
		fprintf (stderr, "command %s not found\n", l.cmds[i].argv[0]);
		return -1;
	}
	if (execcmd (l.cmds[i], path, pid)< 0){
		return -1; 	
	}
	return 0;
}

int
pipeto (linet *l, int i){
	int fds[2];

	if (i < (*l).cmdc-1){
		if (pipe (fds)<0){
			warn ("pipe");
			return -1;
		}
		(*l).cmds[i].fdout = fds[1];
		(*l).cmds[i].nextfdin = fds[0];
	}
	if (i!=0){
		(*l).cmds[i].fdin = (*l).cmds[i-1].nextfdin;
	}
	return 0;
}

int
openstdin(linet *line, int i){

	if((*line).stdin != NULL){
		(*line).cmds[i].fdin = open ((*line).stdin, O_RDONLY);
	}
	if ((*line).stdin == NULL && (*line).bg != 0){
		(*line).cmds[i].fdin = open ("/dev/null", O_RDONLY);
	}
	if ((*line).cmds[i].fdin < 0){
		warn ("open %s", (*line).stdin);
		return -1;
	}
	return 0;
}

int
openstdout (linet *line, int i){
	char cpercent;

	cpercent = '%';
	if (((*line).stdout != NULL) && ((*line).fdspercent[1] != 0)){
		fprintf(stderr, "Cannot use > and %c\n", cpercent);
		return -1;
	}
	if ((*line).stdout != NULL){
		(*line).cmds[i].fdout = creat ((*line).stdout, 0664);
		if ((*line).cmds[i].fdout<0){
			warn ("creat %s", (*line).stdout);
			return -1;
		}
	}
	if ((*line).fdspercent[1] != 0){
		(*line).cmds[i].fdout = (*line).fdspercent[1];
	}
	return 0;
}

int
runcmdline (linet line){
	int i, sts, pid;

	for (i=0;i<line.cmdc;i++){
		if (i==0){
			if (openstdin(&line, i)<0){
				return -1;
			}
		}
		if (i== line.cmdc-1){				
			if (openstdout(&line, i)<0){
				return -1;
			}
		}
		if (line.cmdc > 1){
			if (pipeto (&line, i)<0){
				return -1;
			}
		}
		switch(runcmd (line, i, &pid)){
		case -1:
			if (i==line.cmdc-1){
				return -1;
			}
		case -2:
			return -1;
		case 1:
			return 0;
		}
	}
	if (line.bg == 0){
		while (wait(&sts) != pid){
			;
		}
		setresult(sts);
	}
	return 0;
}

int 
main (void){
	linet line;
	int readlineresult;

	for (;;){
		if (isatty(0)== 1){
			printf ("#:");
		}
		readlineresult = readline (&line);
		if (readlineresult <0){
			setresult(1);
			continue;
		}
		if (readlineresult == 1){
			break;
		}
		if (runcmdline (line)<0){
			setresult(1);
			continue;
		}
	}
	exit (0);
}
