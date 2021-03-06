#include <stdio.h>
#include <assert.h>
#include <stdlib.h> 
#include <math.h> 
#include <string.h>
#include <omp.h>

#include "../../common/timer.h"
#include "../../common/power_cpu.h"

#define STR_SIZE (256)
#define MAX_PD	(3.0e6)
/* required precision in degrees	*/
#define PRECISION	0.001
#define SPEC_HEAT_SI 1.75e6
#define K_SI 100
/* capacitance fitting factor	*/
#define FACTOR_CHIP	0.5


/* chip parameters	*/
float t_chip = 0.0005;
float chip_height = 0.016; float chip_width = 0.016; 
/* ambient temperature, assuming no package at all	*/
float amb_temp = 80.0;

double totalTime = 0;
double energyStart, energyEnd, totalEnergy;

void fatal(char *s)
{
    fprintf(stderr, "Error: %s\n", s);
}

void readinput(float *vect, int grid_rows, int grid_cols, int layers, char *file) {
    int i,j,k;
    FILE *fp;
    char str[STR_SIZE];
    float val;

    if( (fp  = fopen(file, "r" )) ==0 )
    {
      fatal( "The input file could not be opened!" );
      exit(-1);
    }

    for (k=0; k < layers; k++)
      for (i=0; i < grid_rows; i++) 
        for (j=0; j < grid_cols; j++)
        {
          if (fgets(str, STR_SIZE, fp) == NULL) fatal("Error reading file\n");
          if (feof(fp))
            fatal("not enough lines in file");
          if ((sscanf(str, "%f", &val) != 1))
            fatal("invalid file format");
          vect[i*grid_cols+j+k*grid_rows*grid_cols] = val;
        }

    fclose(fp);
}


void writeoutput(float *vect, int grid_rows, int grid_cols, int layers, char *file) {

    int i,j,k, index=0;
    FILE *fp;
    char str[STR_SIZE];

    if( (fp = fopen(file, "w" )) == 0 )
    {
      fatal( "The output file could not be opened!" );
      exit(-1);
    }

    for (k=0; k < layers; k++)
      for (i=0; i < grid_rows; i++) 
        for (j=0; j < grid_cols; j++)
        {
          sprintf(str, "%d\t%g\n", index, vect[i*grid_cols+j+k*grid_rows*grid_cols]);
          fputs(str,fp);
          index++;
        }
    fclose(fp);
}


void computeTempCPU(float *pIn, float* tIn, float *tOut, 
        int nx, int ny, int nz, float Cap, 
        float Rx, float Ry, float Rz, 
        float dt, int numiter) 
{
    float ce, cw, cn, cs, ct, cb, cc;
    float stepDivCap = dt / Cap;
    ce = cw =stepDivCap/ Rx;
    cn = cs =stepDivCap/ Ry;
    ct = cb =stepDivCap/ Rz;

    cc = 1.0 - (2.0*ce + 2.0*cn + 3.0*ct);

    int c,w,e,n,s,b,t;
    int x,y,z;
    int i = 0;
    do{
        for(z = 0; z < nz; z++)
            for(y = 0; y < ny; y++)
                for(x = 0; x < nx; x++)
                {
                    c = x + y * nx + z * nx * ny;

                    w = (x == 0) ? c      : c - 1;
                    e = (x == nx - 1) ? c : c + 1;
                    n = (y == 0) ? c      : c - nx;
                    s = (y == ny - 1) ? c : c + nx;
                    b = (z == 0) ? c      : c - nx * ny;
                    t = (z == nz - 1) ? c : c + nx * ny;


                    tOut[c] = tIn[c]*cc + tIn[n]*cn + tIn[s]*cs + tIn[e]*ce + tIn[w]*cw + tIn[t]*ct + tIn[b]*cb + (dt/Cap) * pIn[c] + ct*amb_temp;
                }
        float *temp = tIn;
        tIn = tOut;
        tOut = temp; 
        i++;
    }
    while(i < numiter);
}

float accuracy(float *arr1, float *arr2, int len)
{
    float err = 0.0; 
    int i;
    for(i = 0; i < len; i++)
    {
        err += (arr1[i]-arr2[i]) * (arr1[i]-arr2[i]);
    }

    return (float)sqrt(err/len);


}

void computeTempOMP(float *pIn, float* tIn, float *tOut, 
        int nx, int ny, int nz, float Cap, 
        float Rx, float Ry, float Rz, 
        float dt, int numiter) 
{
    float ce, cw, cn, cs, ct, cb, cc;
    
    TimeStamp start, end;

    float stepDivCap = dt / Cap;
    ce = cw =stepDivCap/ Rx;
    cn = cs =stepDivCap/ Ry;
    ct = cb =stepDivCap/ Rz;

    cc = 1.0 - (2.0*ce + 2.0*cn + 3.0*ct);

    energyStart = GetEnergyCPU();
    GetTime(start);

#pragma omp parallel
    {
        int count = 0;
        float *tIn_t = tIn;
        float *tOut_t = tOut;

#pragma omp master
        printf("%d threads running\n", omp_get_num_threads());

        do {
            int z; 
#pragma omp for 
            for (z = 0; z < nz; z++) {
                int y;
                for (y = 0; y < ny; y++) {
                    int x;
                    for (x = 0; x < nx; x++) {
                        int c, w, e, n, s, b, t;
                        c =  x + y * nx + z * nx * ny;
                        w = (x == 0)    ? c : c - 1;
                        e = (x == nx-1) ? c : c + 1;
                        n = (y == 0)    ? c : c - nx;
                        s = (y == ny-1) ? c : c + nx;
                        b = (z == 0)    ? c : c - nx * ny;
                        t = (z == nz-1) ? c : c + nx * ny;
                        tOut_t[c] = cc * tIn_t[c] + cn * tIn_t[n] + cs * tIn_t[s] + ce * tIn_t[e] + cw * tIn_t[w] +
                                    ct * tIn_t[t] + cb * tIn_t[b] + (dt/Cap) * pIn[c] + ct * amb_temp;
                    }
                }
            }
            float *t = tIn_t;
            tIn_t = tOut_t;
            tOut_t = t; 
            count++;
        } while (count < numiter);
    }

    GetTime(end);
    energyEnd = GetEnergyCPU();
    totalTime = TimeDiff(start, end);
    totalEnergy = energyEnd - energyStart;

    return; 
} 

void usage(int argc, char **argv)
{
    fprintf(stderr, "Usage: %s <rows/cols> <layers> <iterations> <powerFile> <tempFile> <outputFile>\n", argv[0]);
    fprintf(stderr, "\t<rows/cols>  - number of rows/cols in the grid (positive integer)\n");
    fprintf(stderr, "\t<layers>     - number of layers in the grid (positive integer)\n");
    fprintf(stderr, "\t<iteration>  - number of iterations\n");
    fprintf(stderr, "\t<powerFile>  - name of the file containing the initial power values of each cell\n");
    fprintf(stderr, "\t<tempFile>   - name of the file containing the initial temperature values of each cell\n");
    fprintf(stderr, "\t<outputFile> - output file\n");

    fprintf(stderr, "\tNote: If input file names are not supplied, input is generated randomly.\n");
    exit(1);
}



int main(int argc, char** argv)
{
    int write_out = 0;

    if (argc < 5 || argc > 8)
    {
        usage(argc,argv);
    }

    char *pfile = NULL, *tfile = NULL, *ofile = NULL;
    int iterations = atoi(argv[4]);

    if (argc == 8)
    {
      write_out      = 1;
      pfile          = argv[5];
      tfile          = argv[6];
      ofile          = argv[7];
    }
    else if (argc == 7)
    {
      pfile          = argv[5];
      tfile          = argv[6];
    }
    else if (argc == 6)
    {
      write_out      = 1;
      ofile          = argv[5];
    }

    int numCols      = atoi(argv[1]);
    int numRows      = atoi(argv[2]);
    int layers       = atoi(argv[3]);

    /* calculating parameters*/

    float dx = chip_height/numRows;
    float dy = chip_width/numCols;
    float dz = t_chip/layers;

    float Cap = FACTOR_CHIP * SPEC_HEAT_SI * t_chip * dx * dy;
    float Rx = dy / (2.0 * K_SI * t_chip * dx);
    float Ry = dx / (2.0 * K_SI * t_chip * dy);
    float Rz = dz / (K_SI * dx * dy);

    // cout << Rx << " " << Ry << " " << Rz << endl;
    float max_slope = MAX_PD / (FACTOR_CHIP * t_chip * SPEC_HEAT_SI);
    float dt = PRECISION / max_slope;


    float *powerIn, *tempOut, *tempIn;// *pCopy;
    int size = numCols * numRows * layers;

    powerIn = (float*)calloc(size, sizeof(float));
#ifdef VERIFY
    float* tempCopy = (float*)malloc(size * sizeof(float));
    float* answer = (float*)calloc(size, sizeof(float));
#endif
    tempIn = (float*)calloc(size,sizeof(float));
    tempOut = (float*)calloc(size, sizeof(float));

    if (argc == 7)
    {
        readinput(powerIn, numRows, numCols, layers, pfile);
        readinput(tempIn , numRows, numCols, layers, tfile);
    }
    else
    {
        srand(10);
        int i;
        for (i = 0; i < size; i++)
        {
            powerIn[i] = (float)rand() / (float)(RAND_MAX); // random number between 0 and 1
            tempIn[i] = 300 + (float)rand() / (float)(RAND_MAX/100); // random number between 300 and 400
        }
    }

#ifdef VERIFY
    memcpy(tempCopy,tempIn, size * sizeof(float)); // back up original buffer for verification
#endif

    computeTempOMP(powerIn, tempIn, tempOut, numCols, numRows, layers, Cap, Rx, Ry, Rz, dt, iterations);
    float* OMPOut = (iterations % 2 == 1) ? tempOut : tempIn;

#ifdef VERIFY
    computeTempCPU(powerIn, tempCopy, answer, numCols, numRows, layers, Cap, Rx, Ry, Rz, dt, iterations);
    float* CPUOut = (iterations % 2 == 1) ? answer  : tempCopy;
    float acc = accuracy(OMPOut,CPUOut,numRows*numCols*layers);
    printf("Accuracy: %e\n",acc);
#endif

    printf("Computation done in %0.3lf ms.\n", totalTime);
    if (energyStart != -1) // -1 --> failed to read energy values
    {
       printf("Total energy used is %0.3lf jouls.\n", totalEnergy);
       printf("Average power consumption is %0.3lf watts.\n", totalEnergy/(totalTime/1000.0));
    }

    if (write_out)
    {
        writeoutput(OMPOut,numRows, numCols, layers, ofile);
    }

    free(tempIn);
    free(tempOut); free(powerIn);
    return 0;
}	


