#include <stdio.h>

int main() {
    int n;

    printf("Enter number of processes: ");
    scanf("%d", &n);

    int at[n], bt[n], ct[n], tat[n], wt[n];
    int completed[n];

    // Input
    for (int i = 0; i < n; i++) {
        printf("\nEnter Arrival Time for P%d: ", i + 1);
        scanf("%d", &at[i]);

        printf("Enter Burst Time for P%d: ", i + 1);
        scanf("%d", &bt[i]);

        completed[i] = 0;
    }

    int currentTime = 0;
    int completedCount = 0;

    // SJF Scheduling
    while (completedCount < n) {

        int shortest = -1;

        // Find shortest available process
        for (int i = 0; i < n; i++) {
            if (completed[i] == 0 && at[i] <= currentTime) {

                if (shortest == -1 || bt[i] < bt[shortest]) {
                    shortest = i;
                }
            }
        }

        // If no process has arrived yet
        if (shortest == -1) {
            currentTime++;
            continue;
        }

        // Execute selected process
        currentTime += bt[shortest];

        ct[shortest] = currentTime;
        tat[shortest] = ct[shortest] - at[shortest];
        wt[shortest] = tat[shortest] - bt[shortest];

        completed[shortest] = 1;
        completedCount++;
    }

    // Display result
    printf("\n\nSJF Scheduling:\n");
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

    avgTAT /= n;
    avgWT /= n;

    printf("\nAverage Turnaround Time = %.2f", avgTAT);
    printf("\nAverage Waiting Time = %.2f\n", avgWT);

    return 0;
}
