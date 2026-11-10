#include <stdio.h>

int main() {
    int n, quantum;

    printf("Enter number of processes: ");
    scanf("%d", &n);

    int at[n], bt[n], rt[n], ct[n], tat[n], wt[n];

    // Input Arrival Time and Burst Time
    for (int i = 0; i < n; i++) {
        printf("\nEnter Arrival Time for P%d: ", i + 1);
        scanf("%d", &at[i]);

        printf("Enter Burst Time for P%d: ", i + 1);
        scanf("%d", &bt[i]);

        rt[i] = bt[i];   // Remaining time
    }

    printf("\nEnter Time Quantum: ");
    scanf("%d", &quantum);

    int currentTime = 0;
    int completed = 0;

    // Round Robin Scheduling
    while (completed < n) {

        int executed = 0;

        for (int i = 0; i < n; i++) {

            if (rt[i] > 0 && at[i] <= currentTime) {

                executed = 1;

                if (rt[i] > quantum) {
                    currentTime += quantum;
                    rt[i] -= quantum;
                }
                else {
                    currentTime += rt[i];
                    rt[i] = 0;

                    ct[i] = currentTime;
                    completed++;
                }
            }
        }

        // CPU idle
        if (executed == 0) {
            currentTime++;
        }
    }

    // Calculate TAT and WT
    float totalTAT = 0, totalWT = 0;

    for (int i = 0; i < n; i++) {
        tat[i] = ct[i] - at[i];
        wt[i] = tat[i] - bt[i];

        totalTAT += tat[i];
        totalWT += wt[i];
    }

    // Display result
    printf("\n\nRound Robin Scheduling\n");

    printf("\nProcess\tAT\tBT\tCT\tTAT\tWT\n");

    for (int i = 0; i < n; i++) {
        printf("P%d\t%d\t%d\t%d\t%d\t%d\n",
               i + 1, at[i], bt[i],
               ct[i], tat[i], wt[i]);
    }

    printf("\nAverage Turnaround Time = %.2f",
           totalTAT / n);

    printf("\nAverage Waiting Time = %.2f\n",
           totalWT / n);

    return 0;
}
