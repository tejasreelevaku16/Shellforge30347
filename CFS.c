#include <stdio.h>

struct Process {
    int pid;
    int burst;
    int remaining;
    int vruntime;
};

int main() {
    int n, i, completed = 0;
    int time = 0;
    struct Process p[20];

    printf("Enter number of processes: ");
    scanf("%d", &n);

    for (i = 0; i < n; i++) {
        p[i].pid = i + 1;

        printf("Enter burst time for P%d: ", i + 1);
        scanf("%d", &p[i].burst);

        p[i].remaining = p[i].burst;
        p[i].vruntime = 0;
    }

    printf("\nExecution Order:\n");

    while (completed < n) {
        int min = -1;

        // Find process with smallest virtual runtime
        for (i = 0; i < n; i++) {
            if (p[i].remaining > 0) {
                if (min == -1 || p[i].vruntime < p[min].vruntime) {
                    min = i;
                }
            }
        }

        // Run the selected process for 1 time unit
        printf("P%d ", p[min].pid);

        p[min].remaining--;
        p[min].vruntime++;
        time++;

        // Check whether process is completed
        if (p[min].remaining == 0) {
            completed++;
        }
    }

    printf("\n\nTotal CPU Time = %d\n", time);

    return 0;
}
