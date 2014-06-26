//////////////////////////////////////////////////////////////////////////////////
//    File:         MarsImg.cpp
//    Author:       Miryam Chaabouni
//    Description : Applies SiftGPU on Mars images to match Swiss-cheese features
//		This project was developed in Ecole Polytechnique fédérale de Lausanne 
//		for the Swiss Space Center EPFL
//		It was based on the SimpleSIFT project by Changchang Wu 
//////////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <vector>
#include <iostream>
#include <string>
#include <unordered_map>
#include <fstream>
//#include <ShellAPI.h>
using std::vector;
using std::iostream;

////////////////////////////////////////////////////////////////////////////
#if !defined(SIFTGPU_STATIC) && !defined(SIFTGPU_DLL_RUNTIME) 
// SIFTGPU_STATIC comes from compiler
#define SIFTGPU_DLL_RUNTIME
// Load at runtime if the above macro defined
// comment the macro above to use static linking
#endif

////////////////////////////////////////////////////////////////////////////
// define REMOTE_SIFTGPU to run computation in multi-process (Or remote) mode
// in order to run on a remote machine, you need to start the server manually
// This mode allows you use Multi-GPUs by creating multiple servers
// #define REMOTE_SIFTGPU
// #define REMOTE_SERVER        NULL
// #define REMOTE_SERVER_PORT   7777


///////////////////////////////////////////////////////////////////////////
//#define DEBUG_SIFTGPU  //define this to use the debug version in windows

#ifdef _WIN32
    #ifdef SIFTGPU_DLL_RUNTIME
        #define WIN32_LEAN_AND_MEAN
        #include <windows.h>
        #define FREE_MYLIB FreeLibrary
        #define GET_MYPROC GetProcAddress
    #else
        //define this to get dll import definition for win32
        #define SIFTGPU_DLL
        #ifdef _DEBUG 
            #pragma comment(lib, "../../lib/siftgpu_d.lib")
        #else
            #pragma comment(lib, "../../lib/siftgpu.lib")
        #endif
    #endif
#else
    #ifdef SIFTGPU_DLL_RUNTIME
        #include <dlfcn.h>
        #define FREE_MYLIB dlclose
        #define GET_MYPROC dlsym
    #endif
#endif

#include "../SiftGPU/SiftGPU.h"

//*******************************Global variables***********************************************
//Specify images paths and dimensions 
	std::string inimage1 = " -i ../data/Image1/upleft.JP2 "; 
	std::string inimage2 = " -i ../data/Image2/shifted.JP2 "; 
	int width1 = 5012, height1 = 5000, width2 = 5012, height2 = 5000; //computed with kakadu
	float nRows = 5., nCols = 5.; //float to avoid integer division
	
	//specify thresholds for matching points 
	int thrSCF = 25; //min num of matching points to assess similarity with Swiss cheese feature
	int thrAr = 1500; //min num of matching points to assess similaritiy of two areas 

//***********************************************************************************************
struct Coord { //information about sub-image
	std::string name; //path
	float x0; //coord of upper left corner of subimage 
	float y0; 
};
int main()
{
#ifdef SIFTGPU_DLL_RUNTIME
    #ifdef _WIN32
        #ifdef _DEBUG
            HMODULE  hsiftgpu = LoadLibrary("siftgpu_d.dll");
        #else
            HMODULE  hsiftgpu = LoadLibrary("siftgpu.dll");
        #endif
    #else
        void * hsiftgpu = dlopen("libsiftgpu.so", RTLD_LAZY);
    #endif

    if(hsiftgpu == NULL) return 0;

    #ifdef REMOTE_SIFTGPU
        ComboSiftGPU* (*pCreateRemoteSiftGPU) (int, char*) = NULL;
        pCreateRemoteSiftGPU = (ComboSiftGPU* (*) (int, char*)) GET_MYPROC(hsiftgpu, "CreateRemoteSiftGPU");
        ComboSiftGPU * combo = pCreateRemoteSiftGPU(REMOTE_SERVER_PORT, REMOTE_SERVER);
        SiftGPU* sift = combo;
        SiftMatchGPU* matcher = combo;
    #else
        SiftGPU* (*pCreateNewSiftGPU)(int) = NULL;
        SiftMatchGPU* (*pCreateNewSiftMatchGPU)(int) = NULL;
        pCreateNewSiftGPU = (SiftGPU* (*) (int)) GET_MYPROC(hsiftgpu, "CreateNewSiftGPU");
        pCreateNewSiftMatchGPU = (SiftMatchGPU* (*)(int)) GET_MYPROC(hsiftgpu, "CreateNewSiftMatchGPU");
        SiftGPU* sift = pCreateNewSiftGPU(1);
        SiftMatchGPU* matcher = pCreateNewSiftMatchGPU(4096);
		
    #endif

#elif defined(REMOTE_SIFTGPU)
    ComboSiftGPU * combo = CreateRemoteSiftGPU(REMOTE_SERVER_PORT, REMOTE_SERVER);
    SiftGPU* sift = combo;
    SiftMatchGPU* matcher = combo;
#else
    //this will use overloaded new operators
    SiftGPU  *sift = new SiftGPU;
    SiftMatchGPU *matcher = new SiftMatchGPU(4096);
#endif
	//*********************************************************************************************
    //initialization
    char * argv[] = {"-fo", "-1",  "-v", "0"};
    int argc = sizeof(argv)/sizeof(char*);
    sift->ParseParam(argc, argv);
    if(sift->CreateContextGL() != SiftGPU::SIFTGPU_FULL_SUPPORTED) return 0;
	if(matcher->VerifyContextGL() == 0) return 0; 

	//*********************************************************************************************
	//Split image 1 into blocks and run sift
	std::cout<<"Run SIFT on "<< nRows*nCols<<" blocks of image 1  ..."<<std::endl; 
	vector<Coord> listFiles1; 
	vector<vector<SiftGPU::SiftKeypoint>> keys1; 
	vector<vector<float>> descriptors1 ; 
	//double tileWidth = 1., tileHeight = 1./nRows ; //in percentage of original image size 
	double tilew1 = width1/nCols, tileh1 = height1/nRows;
	//int nfeat1 = 0; 
	for(int c =0; c<nCols; c++){
		for (int r = 0; r<nRows; r++){
		std::string kduInstruct = inimage1 ; //specify input image
		std::string outpath = "../data/Image1/img-"; //specify output image path
		outpath.append(std::to_string(static_cast<long double> (c)) + 
			"-" + std::to_string(static_cast<long double> (r))); //specify output image name
		outpath.append(".pgm "); //specify output image format
			
		kduInstruct.append("-o "+ outpath); 
		kduInstruct.append("-region {"); 
		kduInstruct.append(std::to_string((static_cast<long double> (r/nRows)))+","); //specify row
		kduInstruct.append(std::to_string((static_cast<long double> (c/nCols)))+"}"); //specify column 
		kduInstruct.append(",{"+std::to_string(static_cast<long double> (1./nRows))+","); //specify size of sub-image
		kduInstruct.append(std::to_string(static_cast<long double> (1./nCols))+"}"); 
		kduInstruct.append(" -quiet"); //suppresses informative print statements  
			
		char * kduInstr = new char[kduInstruct.size() + 1];
		std::copy(kduInstruct.begin(), kduInstruct.end(), kduInstr);
		kduInstr[kduInstruct.size()] = '\0';

		PROCESS_INFORMATION ProcessInfo; //This is what we get as an [out] parameter
		STARTUPINFO StartupInfo; //This is an [in] parameter
		ZeroMemory(&StartupInfo, sizeof(StartupInfo));
		StartupInfo.cb = sizeof StartupInfo ; //Only compulsory field
		if(CreateProcess("C:/Program Files (x86)/Kakadu/kdu_expand.exe", 
			kduInstr, 
			NULL,NULL,FALSE,0,NULL,
			NULL,&StartupInfo,&ProcessInfo))
		{ 
			WaitForSingleObject(ProcessInfo.hProcess,INFINITE);
			CloseHandle(ProcessInfo.hThread);
			CloseHandle(ProcessInfo.hProcess);
		}  
		else
		{
			printf("CreateProcess (%d) failed (%d).\n", r*c, GetLastError());
		}
		delete[] kduInstr;
		sift->RunSIFT(outpath.c_str()); 
		int num = sift->GetFeatureNum();
		//nfeat1+=num ; 
		if (num>0){ //if sub-image non empty
			//run sift
			vector<float> descriptors(128*num); 
			vector<SiftGPU::SiftKeypoint> keys(num); 
			sift->GetFeatureVector(&keys[0], &descriptors[0]); 
			//store keypoints, descriptors and coordinates
			keys1.push_back(keys); 
			descriptors1.push_back(descriptors); 
			Coord file = {outpath, c*tilew1, r*tileh1}; 
			listFiles1.push_back(file); 
		}
		}
		printf("%d %% of image 1 processed. \r", (int) ((c+1)*100./nCols)); 
	}
	//printf("%d features detected in image 1. \n",nfeat1) ; 
	//----------------------------------------------------------------------------
	//Split image 2 into blocks and run sift
	std::cout<<"Run SIFT on  "<< nRows*nCols<<"  blocks of image 2  ..."<<std::endl; 
	double tilew2=width2/nCols, tileh2 = height2/nRows; 
	vector<Coord> listFiles2; 
	vector<vector<SiftGPU::SiftKeypoint>> keys2; 
	vector<vector<float>> descriptors2 ;
	//int nfeat2 =0; 
	for(int c =0; c<nCols; c++){
		for (int r = 0; r<nRows; r++){
			std::string kduInstruct = inimage2 ; //specify input image
			std::string outpath = "../data/Image2/img-"; //specify output image path
			outpath.append(std::to_string(static_cast<long double> (c)) + 
				"-" + std::to_string(static_cast<long double> (r))); //specify output image name
			outpath.append(".pgm "); //specify output image format
			
			kduInstruct.append("-o "+ outpath); 
			kduInstruct.append("-region {"); 
			kduInstruct.append(std::to_string((static_cast<long double> (r/nRows)))+","); //specify row
			kduInstruct.append(std::to_string((static_cast<long double> (c/nCols)))+"}"); //specify column 
			kduInstruct.append(",{"+std::to_string(static_cast<long double> (1./nRows))+","); //specify size of sub-image
			kduInstruct.append(std::to_string(static_cast<long double> (1./nCols))+"}"); 
			kduInstruct.append(" -quiet"); //suppresses informative print statements  
			
			char * kduInstr = new char[kduInstruct.size() + 1];
			std::copy(kduInstruct.begin(), kduInstruct.end(), kduInstr);
			kduInstr[kduInstruct.size()] = '\0';

			PROCESS_INFORMATION ProcessInfo; //This is what we get as an [out] parameter
			STARTUPINFO StartupInfo; //This is an [in] parameter
			ZeroMemory(&StartupInfo, sizeof(StartupInfo));
			StartupInfo.cb = sizeof StartupInfo ; //Only compulsory field
			if(CreateProcess("C:/Program Files (x86)/Kakadu/kdu_expand.exe", 
				kduInstr, 
				NULL,NULL,FALSE,0,NULL,
				NULL,&StartupInfo,&ProcessInfo))
			{ 
				WaitForSingleObject(ProcessInfo.hProcess,INFINITE);
				CloseHandle(ProcessInfo.hThread);
				CloseHandle(ProcessInfo.hProcess);
			}  
			else
			{
				printf("CreateProcess (%d) failed (%d).\n", r*c, GetLastError());
			}
			delete[] kduInstr;

			sift->RunSIFT(outpath.c_str()); 
			int num = sift->GetFeatureNum();
			//nfeat2+=num ; 
			if (num>0){ //if sub-image non empty
				//run sift
				vector<float> descriptors(128*num); 
				vector<SiftGPU::SiftKeypoint> keys(num); 
				sift->GetFeatureVector(&keys[0], &descriptors[0]); 
				//store keypoints, descriptors and coordinates
				keys2.push_back(keys); 
				descriptors2.push_back(descriptors); 
				Coord file = {outpath, c*tilew2, r*tileh2}; 
				listFiles2.push_back(file); 
			}
		}
		printf("%d %% of image 2 processed. \r", (int) ((c+1)*100./nCols)); 
	}
	//printf("%d features detected in image 2. \n",nfeat2) ; 
	//----------------------------------------------------------------------------
	//reference spheres
	std::cout<<"Running Sift on 3 Reference Spheres ...\n"; 
	vector<vector<float>> descriptorS(3);
	vector<vector<SiftGPU::SiftKeypoint>> keysS(3);    
	int numS[3] = {0,0,0};
	char* refSph[3] = {"../data/RefSCF/ref-sphere-3.pgm", "../data/RefSCF/ref-sphere-5.pgm", "../data/RefSCF/ref-sphere-6.pgm"}; 
	sift->SetImageList(3, (const char **) refSph); 
	
	for(int i = 0; i<3; ++i){
	if(sift->RunSIFT(i))
		{
			numS[i] = sift->GetFeatureNum();
			keysS[i].resize(numS[i]);    descriptorS[i].resize(128*numS[i]);
			sift->GetFeatureVector(&keysS[i][0], &descriptorS[i][0]);     
		}
	}

	//***********************************************************************************
	//Matching 
	std::ofstream outfile;
	outfile.open ("../data/matching_points.txt");
	

	std::cout << "Matching... \n";
	for(int j = 0; j<keys1.size(); j++){
		for(int k =0; k<keys2.size(); k++){
			int num1 = keys1[j].size(), num2 = keys2[k].size(); //number of features
			//matching two images
			matcher->SetDescriptors(0, num1, &descriptors1[j][0]); //image 1
			matcher->SetDescriptors(1, num2, &descriptors2[k][0]); //image 2
 
			int (*match_buf)[2] = new int[num1][2];
			int num_match = matcher->GetSiftMatch(num1, match_buf);
			//use to estimate thrAr with same inputs inimage1 and inimage2
			/*
			if(j==k){ 
				std::cout<<num_match<<" matches were found"<<std::endl;
			}*/
			//if similar areas, re-run sift on matching keypoints of image 1
			if (num_match > thrAr) {
				vector<float> descriptorm1(128*num_match); 
				vector<SiftGPU::SiftKeypoint> keym1(num_match); 
				std::unordered_map<int, int> match_buf_map ;
				std::unordered_map<int, int> map_1to1S ;
				for(int l =0; l<num_match; l++){
					keym1[l] = keys1[j][match_buf[l][0]]; //get matching features
					match_buf_map.insert(std::pair<int, int>(match_buf[l][0], match_buf[l][1]));  //this will facilitate search of corresponding keypoints
					map_1to1S.insert(std::pair<int, int>(l, match_buf[l][0])); 
				}	

				//re-run sift only on specific keypoints to get new descriptors
				sift->SetKeypointList(keym1.size(), &keym1[0]); 
				sift->RunSIFT(listFiles1[j].name.c_str()); 
				sift->GetFeatureVector(NULL, &descriptorm1[0]);  
				int num = sift->GetFeatureNum(); 
				//match with 3 reference SCF
				int (*match_buf_0)[2] = new int[num][2];
				int (*match_buf_1)[2] = new int[num][2];
				int (*match_buf_2)[2] = new int[num][2];

				matcher->SetDescriptors(0, num_match, &descriptorm1[0]); //image 1

				matcher->SetDescriptors(1, numS[0], &descriptorS[0][0]); 
				int nmatch_0 = matcher->GetSiftMatch(num, match_buf_0); 
				
				matcher->SetDescriptors(1, numS[1], &descriptorS[1][0]); 
				int nmatch_1 = matcher->GetSiftMatch(num, match_buf_1); 
	
				matcher->SetDescriptors(1, numS[2], &descriptorS[2][0]); 
				int nmatch_2 = matcher->GetSiftMatch(num, match_buf_2); 
				
				//use to estimate SCF threshold 
				//printf("%d matching points corresponding to an SCF found. \n", nmatch_0+nmatch_1+nmatch_2); 
				if(nmatch_0+nmatch_1+nmatch_2 > thrSCF){//write to file

					for (int i = 0; i<nmatch_0; i++){
						double x1, y1, x2, y2; //absolute coordinates in full image
						x1 = listFiles1.at(j).x0; 
						y1 = listFiles1.at(j).y0; 
						//find corresponding points in image 2
						x2 = listFiles2.at(k).x0; 
						y2 = listFiles2.at(k).y0; 
						std::unordered_map<int,int>::const_iterator got_1to1S = map_1to1S.find(match_buf_0[i][0]);
						if ( ! (got_1to1S == map_1to1S.end() )){
							x1 += keys1[j][got_1to1S->second].x; 
							y1 += keys1[j][got_1to1S->second].y; 
							std::unordered_map<int,int>::const_iterator got_1to2 = match_buf_map.find(got_1to1S->second);
							if ( ! (got_1to2 == match_buf_map.end() )){
								x2 += keys2[k][got_1to2->second].x; 
								y2 += keys2[k][got_1to2->second].y; 
							}
						}
						else {std::cout<<"Error : Second key not found!\n"; }
						outfile<<x1 <<" "<<y1<<" "<<x2<<" "<<y2<<"\n";
						
					} 

					for (int i = 0; i<nmatch_1; i++){
						double x1, y1, x2, y2; //absolute coordinates in full image
						x1 = listFiles1.at(j).x0; 
						y1 = listFiles1.at(j).y0; 
						//find corresponding points in image 2
						x2 = listFiles2.at(k).x0; 
						y2 = listFiles2.at(k).y0; 
						std::unordered_map<int,int>::const_iterator got_1to1S = map_1to1S.find(match_buf_1[i][0]);
						if ( ! (got_1to1S == map_1to1S.end() )){
							x1 += keys1[j][got_1to1S->second].x; 
							y1 += keys1[j][got_1to1S->second].y; 
							std::unordered_map<int,int>::const_iterator got_1to2 = match_buf_map.find(got_1to1S->second);
							if ( ! (got_1to2 == match_buf_map.end() )){
								x2 += keys2[k][got_1to2->second].x; 
								y2 += keys2[k][got_1to2->second].y; 
							}
						}
						else {std::cout<<"Error : Second key not found!\n"; }
						outfile<<x1 <<" "<<y1<<" "<<x2<<" "<<y2<<"\n";
						
					} 
					for (int i = 0; i<nmatch_2; i++){
						double x1, y1, x2, y2; //absolute coordinates in full image
						x1 = listFiles1.at(j).x0; 
						y1 = listFiles1.at(j).y0; 
						//find corresponding points in image 2
						x2 = listFiles2.at(k).x0 ; 
						y2 = listFiles2.at(k).y0 ; 
						std::unordered_map<int,int>::const_iterator got_1to1S = map_1to1S.find(match_buf_2[i][0]);
						if ( ! (got_1to1S == map_1to1S.end() )){
							x1 += keys1[j][got_1to1S->second].x; 
							y1 += keys1[j][got_1to1S->second].y; 
							std::unordered_map<int,int>::const_iterator got_1to2 = match_buf_map.find(got_1to1S->second);
							if ( ! (got_1to2 == match_buf_map.end() )){
								x2 += keys2[k][got_1to2->second].x; 
								y2 += keys2[k][got_1to2->second].y; 
							}
						}
						else {std::cout<<"Error : Second key not found!\n"; }
						outfile<<x1 <<" "<<y1<<" "<<x2<<" "<<y2<<"\n";
						
					} 
				}
				delete[] match_buf_0, match_buf_1, match_buf_2; 
			}
		}
		printf("%d %% matching processed. \r", (int) ((j+1)*100./keys1.size())); 
	}
outfile.close();


#ifdef REMOTE_SIFTGPU
    delete combo;
#else
    delete sift;
    delete matcher;
#endif

#ifdef SIFTGPU_DLL_RUNTIME
    FREE_MYLIB(hsiftgpu);
#endif
    return 1;
}
