#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <pthread.h>
#include "queue.h"

#define hpCount 1
#define mpCount 3
#define lpCount 6
#define totalSellCount (hpCount + mpCount + lpCount)
#define concertRow 10
#define concertCol 10
#define simulationDuration 60

// Seller Argument Structure
typedef struct sellArgStruct {
	char sellerNo;
	char sellerType;
	Queue *sellerQueue;
} sell_arg;

//Customer Structure
typedef struct customerStruct {
	char custNo;
	int arrivalTime;
} customer;

//Global Variable
int simTime;
int N = 5; //N customers in each seller queue
int at1[15] = {0}, st1[15] = {0}, tat1[15] = {0}, bt1[15]={0}, rt1[15]={0};
float throughput[3] = {0}, processesStarted[3] = {0};
int response_time[3] = {0}, turn_around_time[3] = {0}; // per seller type 0:L, 1:M, 2:H.

float avg_rt=0, avg_tat=0, num_cust_served = 0;
char seat_matrix[concertRow][concertCol][5];	//4 to hold L002\0

//Thread Variable
pthread_t seller_t[totalSellCount];
pthread_mutex_t threadCount_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t thread_waiting_for_clock_tick_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t reservation_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t thread_completion_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t condition_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  condition_cond  = PTHREAD_COND_INITIALIZER;

//Function Definitions
void display_queue(Queue *q);
void create_seller_threads(pthread_t *thread, char sellerType, int no_of_sellers);
void wait_for_thread_to_serve_current_time_slice();
void wakeup_all_seller_threads();
void *sell(void *);
Queue * create_customer_queue(int);
int compare_by_arrivalTime(void * data1, void * data2);
int fetchEmptySeatIndexBySellerType(char sellerType);

int threadCount = 0;
int threadsWaitingForClockTick = 0;
int activeThread = 0;
int verbose = 0;

int main(int argc, char** argv) {
    srand(4388);

	if(argc == 2) {
		N = atoi(argv[1]);
	}

	//Initialize Global Variables
	for(int r=0; r<concertRow; r++) {
		for(int c=0; c<concertCol; c++) {
			strncpy(seat_matrix[r][c],"-",1);
		}
	}

	//Create all threads
	create_seller_threads(seller_t, 'H', hpCount);
	create_seller_threads(seller_t + hpCount, 'M', mpCount);
	create_seller_threads(seller_t + hpCount + mpCount, 'L', lpCount);

	//Wait for threads to finish initialization and wait for synchronized clock tick
	while(1) {
		pthread_mutex_lock(&threadCount_mutex);
		if(threadCount == 0) {
			pthread_mutex_unlock(&threadCount_mutex);
			break;
		}
		pthread_mutex_unlock(&threadCount_mutex);
	}

	//Simulate each time quanta/slice as one iteration
	printf("Starting Simulation: \n");
	threadsWaitingForClockTick = 0;
	wakeup_all_seller_threads(); //For first tick
	
	do {

		//Wake up all thread
		wait_for_thread_to_serve_current_time_slice();
		simTime = simTime + 1;
		wakeup_all_seller_threads();
		//Wait for thread completion
	} while(simTime < simulationDuration);

	//Wakeup all thread so that no more thread keep waiting for clock Tick in limbo
	wakeup_all_seller_threads();

	while(activeThread);

	//Display concert chart
	printf("\n\n");
	printf("Final Concert Seat Chart\n");
	printf("========================\n");

	int h_customers = 0,m_customers = 0,l_customers = 0;
	for(int r=0;r<concertRow;r++) {
		for(int c=0;c<concertCol;c++) {
			if(c!=0)
				printf("\t");
			printf("%5s",seat_matrix[r][c]);
			if(seat_matrix[r][c][0]=='H') h_customers++;
			if(seat_matrix[r][c][0]=='M') m_customers++;
			if(seat_matrix[r][c][0]=='L') l_customers++;
		}
		printf("\n");
	}

	printf("\n\nStat for N = %02d\n",N);
	printf("===============\n");
	printf(" ================================================\n");
	printf("|%3c | Number of Customers | Got Seat | Returned |\n",' ');
	printf(" ================================================\n");
	printf("|%3c | %19d | %8d | %8d |\n",'H',hpCount*N,h_customers,(hpCount*N)-h_customers);
	printf("|%3c | %19d | %8d | %8d |\n",'M',mpCount*N,m_customers,(mpCount*N)-m_customers);
	printf("|%3c | %19d | %8d | %8d |\n",'L',lpCount*N,l_customers,(lpCount*N)-l_customers);
	printf(" ================================================\n");

    for(int z1=0; z1<N; z1++){
        int ct = 0;
        ct = st1[z1] + bt1[z1];
        rt1[z1] = abs(st1[z1]-at1[z1]);
        tat1[z1] = abs(ct - at1[z1]);
    }

    for(int j1=0; j1<N; j1++){
        avg_tat += tat1[j1];
        avg_rt += rt1[j1];
    }

	printf("Average Response time of seller L is %.2f\n", response_time[0]/processesStarted[0]);
    printf("Average Response time  of seller M is %.2f\n", response_time[1]/processesStarted[1]);
    printf("Average Response time  of seller H is %.2f\n", response_time[2]/processesStarted[2]);

	printf("Average Turn Around time of seller L is %.2f\n", turn_around_time[0]/throughput[0]);
    printf("Average Turn Around time  of seller M is %.2f\n", turn_around_time[1]/throughput[1]);
    printf("Average Turn around  time  of seller H is %.2f\n", turn_around_time[2]/throughput[2]);

    printf("Throughput of seller L is %.2f\n", throughput[0]/60.0);
    printf("Throughput of seller M is %.2f\n", throughput[1]/60.0);
    printf("Throughput of seller H is %.2f\n", throughput[2]/60.0);

	return 0;
}

void create_seller_threads(pthread_t *thread, char sellerType, int no_of_sellers){
	//Create all threads
	for(int t_no = 0; t_no < no_of_sellers; t_no++) {
		sell_arg *seller_arg = (sell_arg *) malloc(sizeof(sell_arg));
		seller_arg->sellerNo = t_no;
		seller_arg->sellerType = sellerType;
		seller_arg->sellerQueue = create_customer_queue(N);

		pthread_mutex_lock(&threadCount_mutex);
		threadCount++;
		pthread_mutex_unlock(&threadCount_mutex);
		if(verbose)
			printf("Creating thread %c%02d\n",sellerType,t_no);
		pthread_create(thread+t_no, NULL, &sell, seller_arg);
	}
}

void display_queue(Queue *q) {
	for(Node *ptr = q->front;ptr!=NULL;ptr=ptr->next) {
		customer *cust = (customer * )ptr->data;
		printf("[%d,%d]",cust->custNo,cust->arrivalTime);
	}
}

void wait_for_thread_to_serve_current_time_slice(){
	//Check if all threads has finished their jobs for this time slice
	while(1){
		pthread_mutex_lock(&thread_waiting_for_clock_tick_mutex);
		if(threadsWaitingForClockTick == activeThread) {
			threadsWaitingForClockTick = 0;	
			pthread_mutex_unlock(&thread_waiting_for_clock_tick_mutex);
			break;
		}
		pthread_mutex_unlock(&thread_waiting_for_clock_tick_mutex);
	}
}
void wakeup_all_seller_threads() {

	pthread_mutex_lock( &condition_mutex );
	if(verbose)
		printf("00:%02d Main Thread Broadcasting Clock Tick\n",simTime);
	pthread_cond_broadcast( &condition_cond);
	pthread_mutex_unlock( &condition_mutex );
}

void *sell(void *t_args) {
	//Initializing thread
	sell_arg *args = (sell_arg *) t_args;
	Queue * customer_queue = args->sellerQueue;
	Queue * sellerQueue = createQueue();
	char sellerType = args->sellerType;
	int sellerNo = args->sellerNo + 1;
	
	pthread_mutex_lock(&threadCount_mutex);
	threadCount--;
	activeThread++;
	pthread_mutex_unlock(&threadCount_mutex);

	customer *cust = NULL;
	int random_wait_time = 0;
    int temp1 = 0;
	

	while(simTime < simulationDuration) {
		//Waiting for clock tick
		pthread_mutex_lock(&condition_mutex);
		if(verbose)
			printf("00:%02d %c%02d Waiting for next clock tick\n",simTime,sellerType,sellerNo);
		
		pthread_mutex_lock(&thread_waiting_for_clock_tick_mutex);
		threadsWaitingForClockTick++;
		pthread_mutex_unlock(&thread_waiting_for_clock_tick_mutex);
		
		pthread_cond_wait( &condition_cond, &condition_mutex);
		if(verbose)
			printf("00:%02d %c%02d Received Clock Tick\n",simTime,sellerType,sellerNo);
		pthread_mutex_unlock( &condition_mutex );

		// Sell
		if(simTime == simulationDuration) break;
		//All New Customer Came
		while(customer_queue->size > 0 && ((customer *)customer_queue->front->data)->arrivalTime <= simTime) {
			customer *temp = (customer *) dequeue (customer_queue);
			enqueue(sellerQueue,temp);
			printf("00:%02d %c%d Customer %c%d%02d arrived\n",simTime,sellerType,sellerNo,sellerType,sellerNo,temp->custNo);
		}
		//Serve next customer
		if(cust == NULL && sellerQueue->size>0) {
			cust = (customer *) dequeue(sellerQueue);
			printf("00:%02d %c%d Serving Customer %c%d%02d\n",simTime,sellerType,sellerNo,sellerType,sellerNo,cust->custNo);
			switch(sellerType) {
				case 'H':
				random_wait_time = (rand()%2) + 1;
                bt1[temp1] = random_wait_time;
                temp1++;
				response_time[2] += (simTime - cust->arrivalTime);
				processesStarted[2] += 1;
				break;
				case 'M':
				random_wait_time = (rand()%3) + 2;
                bt1[temp1] = random_wait_time;
                temp1++;
				response_time[1] += (simTime - cust->arrivalTime);
				processesStarted[1] += 1;
				break;
				case 'L':
				random_wait_time = (rand()%4) + 4;
                bt1[temp1] = random_wait_time;
                temp1++;
				response_time[0] += (simTime - cust->arrivalTime);
				processesStarted[0] += 1;
			}
		}
		if(cust != NULL) {
			//printf("Wait time %d\n",random_wait_time);
			if(random_wait_time == 0) {
				//Selling Seat
				pthread_mutex_lock(&reservation_mutex);

				// Find seat
				int seatIndex = fetchEmptySeatIndexBySellerType(sellerType);
				if(seatIndex == -1) {
					printf("00:%02d %c%d Customer %c%d%02d has been told Concert Sold Out.\n",simTime,sellerType,sellerNo,sellerType,sellerNo,cust->custNo);
				} else {
					int row_no = seatIndex/concertCol;
					int col_no = seatIndex%concertCol;
					sprintf(seat_matrix[row_no][col_no],"%c%d%02d",sellerType,sellerNo,cust->custNo);
					printf("00:%02d %c%d Customer %c%d%02d assigned seat %d,%d \n",simTime,sellerType,sellerNo,sellerType,sellerNo,cust->custNo,row_no+1,col_no+1);
                    num_cust_served++;
                    if (sellerType == 'L') {
                        throughput[0]++;
						turn_around_time[0] += (simTime - cust->arrivalTime);
					} else if (sellerType=='M') {
                        throughput[1]++;
						turn_around_time[1] += (simTime - cust->arrivalTime);
					}
                    else if (sellerType == 'H') {
                        throughput[2]++;
						turn_around_time[2] += (simTime - cust->arrivalTime);
					}
				}
				pthread_mutex_unlock(&reservation_mutex);
				cust = NULL;
			} else {
				random_wait_time--;
			}
		} else {
			//printf("00:%02d %c%02d Waiting for customer\n",simTime,sellerType,sellerNo);
		}
	}

	while(cust!=NULL || sellerQueue->size > 0) {
		if(cust==NULL)
			cust = (customer *) dequeue(sellerQueue);
		printf("00:%02d %c%d Ticket Sale Closed. Customer %c%d%02d Leaves\n",simTime,sellerType,sellerNo,sellerType,sellerNo,cust->custNo);
		cust = NULL;
	}

	pthread_mutex_lock(&threadCount_mutex);
	activeThread--;
	pthread_mutex_unlock(&threadCount_mutex);
	return NULL;
}

int isSeatAvailable(int row, int col) {
	return (strcmp(seat_matrix[row][col],"-") == 0);
}

int getSeatIndex(int row, int col) {
	return row * concertCol + col;
} 

int fetchEmptySeatIndexForHighSeller() {
	int seat_index = -1, row = 0, col = 0;
	while (row >= 0 && col >= 0 && row < concertRow && col < concertCol) {
		if (isSeatAvailable(row, col)) {
			seat_index = getSeatIndex(row, col);
			break;
		}
		col += 1;
		if (col == concertCol) { row += 1; col = 0;}
	}
	return seat_index;
}

int fetchEmptySeatIndexForMediumSeller() {
	int seat_index = -1, row = concertRow/2-1, col = 0, middleSeatIncrement = 1;
	while (row >= 0 && col >= 0 && row < concertRow && col < concertCol) {
		if (isSeatAvailable(row, col)) {
			seat_index = getSeatIndex(row, col);
			break;
		}
		col += 1;
		if (col == concertCol) {
			if (row % 2 == 0) {
				row = row + middleSeatIncrement;
			} else {
				row = row  - middleSeatIncrement;
			}
			middleSeatIncrement += 1;
			col = 0;
		}
	}
	return seat_index;
}

int fetchEmptySeatIndexForLowSeller() {
	int seat_index = -1, row = concertRow - 1, col = concertCol - 1;
	while(row >= 0 && col >= 0 && row < concertRow && col < concertCol) {
		if (isSeatAvailable(row, col)) {
			seat_index = getSeatIndex(row, col);
			break;
		}
		col -= 1;
		if (col == -1) { row -= 1; col += concertCol;}
	}
	return seat_index;
}

int fetchEmptySeatIndexBySellerType(char sellerType) {
	switch(sellerType) {
		case 'H':
			return fetchEmptySeatIndexForHighSeller();
		case 'M':
			return fetchEmptySeatIndexForMediumSeller();
		case 'L':
			return fetchEmptySeatIndexForLowSeller();
		default:
			return -1;
	}
}

Queue * create_customer_queue(int num_customers){
	Queue * customer_queue = createQueue();
	char custNo = 0;
	while(num_customers--) {
		customer *cust = (customer *) malloc(sizeof(customer));
		cust->custNo = custNo;
		cust->arrivalTime = rand() % simulationDuration;
        at1[custNo] = cust->arrivalTime;
		enqueue(customer_queue,cust);
		custNo++;
	}
	sort(customer_queue, compare_by_arrivalTime);
	Node * ptr = customer_queue->front;
	custNo = 0;
	while(ptr!=NULL) {
		custNo ++;
		customer *cust = (customer *) ptr->data;
		cust->custNo = custNo;
		ptr = ptr->next;
	}
	return customer_queue;
}

int compare_by_arrivalTime(void * data1, void * data2) {
	customer *customer1 = (customer *)data1;
	customer *customer2 = (customer *)data2;
	if(customer1->arrivalTime < customer2->arrivalTime) {
		return -1;
	} else if(customer1->arrivalTime == customer2->arrivalTime){
		return 0;
	} else {
		return 1;
	}
}