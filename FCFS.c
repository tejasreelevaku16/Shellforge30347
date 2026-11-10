#include <stdio.h>

int main() {
    int n;
    
    printf("Enter number of processes: ");
    scanf("%d", &n);

    int at[n], bt[n], ct[n], tat[n], wt[n];

    // Input Arrival Time and Burst Time
    for (int i = 0; i < n; i++) {
        printf("\nEnter Arrival Time for P%d: ", i + 1);
        scanf("%d", &at[i]);

        printf("Enter Burst Time for P%d: ", i + 1);
        scanf("%d", &bt[i]);
    }

    // FCFS Scheduling
    int currentTime = 0;

    for (int i = 0; i < n; i++) {

        // If CPU is idle
        if (currentTime < at[i]) {
            currentTime = at[i];
        }

        // Completion Time
        currentTime = currentTime + bt[i];
        ct[i] = currentTime;

        // Turnaround Time
        tat[i] = ct[i] - at[i];

        // Waiting Time
        wt[i] = tat[i] - bt[i];
    }

    // Display results
    printf("\n\nFCFS Scheduling:\n");

    printf("\nProcess\tAT\tBT\tCT\tTAT\tWT\n");

    for (int i = 0; i < n; i++) {
        printf("P%d\t%d\t%d\t%d\t%d\t%d\n",
               i + 1, at[i], bt[i], ct[i], tat[i], wt[i]);
    }

    // Calculate averages
    float avgTAT = 0, avgWT = 0;

    for (int i = 0; i < n; i++) {
        avgTAT += tat[i];
        avgWT += wt[i];
    }

    avgTAT = avgTAT / n;
    avgWT = avgWT / n;

    printf("\nAverage Turnaround Time = %.2f", avgTAT);
    printf("\nAverage Waiting Time = %.2f\n", avgWT);

    return 0;
}
