#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <testkit.h>
#include "labyrinth.h"
#include <getopt.h>
#include <unistd.h>

#define MAX_MAP 101

int main(int argc, char *argv[]) {
    
    int map_num = 0;
    char *map_path = NULL;
    int id_num = 0;
    char *player_id = NULL;
    int move_num = 0;
    char *move = NULL;
    int version = 0;
    //see the argvs
    //get options------------------------------------------------------------------------------------------
    int opt;
    static struct option options[] = {
        {"map", required_argument, NULL, 'm'},
        {"player", required_argument, NULL, 'p'},
        {"move", required_argument, NULL, 1},
        {"version", no_argument, NULL, 2},
        {NULL, 0, NULL, 0},
    };
    while ((opt = getopt_long(argc, argv, "m:p:", options, NULL)) != -1){
        switch (opt)
        {
        case 'm':
            map_num++;
            map_path = optarg;
            //printf("the map path is %s\n", optarg);
            break;
        case 'p':
            id_num++;
            player_id = optarg;
            //printf("player id is %s\n", optarg);
            break;
        case 1:
            move_num++;
            move = optarg;
            //printf("move to %s\n", optarg);
            break;
        case 2:
            version++;
            //printf("VERSION_INFO\n");
            break;
        default:
            fprintf(stderr, "Invalid option %c\n", optopt);
            printUsage();
            exit(EXIT_FAILURE);
            break;
        }
    }
    if (optind<argc){
        fprintf(stderr, "Argv lefted!\n");
        printUsage();
        exit(EXIT_FAILURE);
    }
    
    if (version!=0){
        if (map_num==0&&id_num==0&&move_num==0){
            printf(VERSION_INFO);
            exit(EXIT_SUCCESS);
        }
        else{
            fprintf(stderr, "Version should be alone\n");
            exit(EXIT_FAILURE);
        }
    }

    if (map_num != 1 || id_num != 1){
        fprintf(stderr, "Must have a map and a player\n");
        exit(EXIT_FAILURE);
    }
    
    Labyrinth MAP;
    if (!loadMap(&MAP, map_path)){
        fprintf(stderr, "the map is not good\n");
        exit(EXIT_FAILURE);
    }
    else{
        if (!isConnected(&MAP)){
            fprintf(stderr, "Map is not connected\n");
            exit(EXIT_FAILURE);
        }
        for (int i=0;i<MAP.rows;i++){
            for (int j=0;j<MAP.cols;j++){
                printf("%c", MAP.map[i][j]);
            }
            printf("\n");
        }
    }
    
    if (player_id[1]!='\0'||!isValidPlayer(player_id[0])){//check playerid
        fprintf(stderr, "Player id false\n");
        exit(EXIT_FAILURE);
    }
    
    if (move_num>1){
        fprintf(stderr, "to many movements\n");
        exit(EXIT_FAILURE);
    }
    
    bool move_state = movePlayer(&MAP, player_id[0], move);
    if (!move_state){
        fprintf(stderr, "Bad move\n");
        exit(EXIT_FAILURE);
    }
    else{
        if (!saveMap(&MAP, map_path)){
            fprintf(stderr, "Save map false\n");
            exit(EXIT_FAILURE);
        }
        exit(EXIT_SUCCESS);
    }

    return 0;
}

void printUsage() {
    printf("Usage:\n");
    printf("  labyrinth --map map.txt --player id\n");
    printf("  labyrinth -m map.txt -p id\n");
    printf("  labyrinth --map map.txt --player id --move direction\n");
    printf("  labyrinth --version\n");
}

bool isValidPlayer(char playerId) {
    return playerId>='0'&&playerId<='9';
    /*int num = playerId-'0';
    if (num>=0&&num<=9){
        return true;
    }
    return false;*/
}

bool loadMap(Labyrinth *labyrinth, const char *filename) {
    //read maps into array
    FILE *file = fopen(filename, "r");
    if (file==NULL){
        return false;
    }
    int rows = 0;
    int cols = 0;
    char buffer[MAX_MAP];//no more than 100
    //--------------------------------------------------------
    while (fgets(buffer, sizeof(buffer), file) != NULL){       
        int this_cols = 0;
        while (buffer[this_cols]!='\n'&&buffer[this_cols]!='\0'){
            if (this_cols>100){
                fprintf(stderr, "too big map\n");
                fclose(file);
                return false;
            }
            labyrinth->map[rows][this_cols] = buffer[this_cols];
            this_cols++;
        }
        if (cols==0){
            cols = this_cols;
        }
        else{
            if (cols!=this_cols){
                fprintf(stderr, "the maps cols is not same\n");
                fclose(file);
                return false;
            }
        }
        rows++;
        if (rows>100){
            fprintf(stderr, "too big map\n");
            fclose(file);
            return false;
        }
    }
    labyrinth->rows = rows;
    labyrinth->cols = cols;
    fclose(file);
    //check the map's rows and cols----------------------------
    return (rows<=100&&cols<=100);
}

Position findPlayer(Labyrinth *labyrinth, char playerId) {
    Position pos = {-1, -1};
    int row_s = labyrinth->rows;
    int col_s = labyrinth->cols;
    for (int i=0;i<row_s;i++){
        for (int j=0;j<col_s;j++){
            if (labyrinth->map[i][j]==playerId){
                pos.row = i;
                pos.col = j;
                return pos;
            }
        }
    }
    return pos;
}

Position findFirstEmptySpace(Labyrinth *labyrinth) {
    Position pos = {-1, -1};
    int row_s = labyrinth->rows;
    int col_s = labyrinth->cols;
    for (int i=0;i<row_s;i++){
        for (int j=0;j<col_s;j++){
            if (labyrinth->map[i][j]=='.'){
                pos.row = i;
                pos.col = j;
                return pos;
            }
        }
    }
    return pos;
}

bool isEmptySpace(Labyrinth *labyrinth, int row, int col) {
    if (labyrinth->map[row][col]=='.'){
        return true;
    }
    return false;
}

bool movePlayer(Labyrinth *labyrinth, char playerId, const char *direction) {
    //change the map info
    Position play_loc = findPlayer(labyrinth, playerId);
    int play_x = play_loc.row;
    int play_y = play_loc.col;
    if (play_loc.row==-1 && play_loc.col==-1){
        //find no player
        //find a empty space to place it
        Position empty_1st = findFirstEmptySpace(labyrinth);
        if (empty_1st.row==-1 && empty_1st.col==-1){
            fprintf(stderr, "no empty place for player\n");
            return false;
        }
        labyrinth->map[empty_1st.row][empty_1st.col] = playerId;
        return true;
    }

    else if (direction==NULL){
        return true;//no move
    }

    else{
        switch (direction[0])
        {
        case 'u':
            play_loc.row--;
            if (play_loc.row<0){
                fprintf(stderr, "out side\n");
                return false;
            }
            break;
        case 'd':
            play_loc.row++;
            if (play_loc.row>labyrinth->rows){
                fprintf(stderr, "out side\n");
                return false;
            }
            break;
        case 'l':
            play_loc.col--;
            if (play_loc.col<0){
                fprintf(stderr, "out side\n");
                return false;
            }
            break;
        case 'r':
            play_loc.col++;
            if (play_loc.col>labyrinth->cols){
                fprintf(stderr, "out side\n");
                return false;
            }
            break;
        default:
            fprintf(stderr, "false direction\n");
            return false;
            break;
        }
        if (labyrinth->map[play_loc.row][play_loc.col] != '.'){
            fprintf(stderr, "target loc is not empty\n");
            return false;
        }
        labyrinth->map[play_loc.row][play_loc.col] = playerId;
        labyrinth->map[play_x][play_y] = '.';
    }
    return true;
}

bool saveMap(Labyrinth *labyrinth, const char *filename) {
    FILE *file = fopen(filename, "w");
    if (file == NULL){
        return false;
    }
    int rows = labyrinth->rows;
    int cols = labyrinth->cols;
    for (int i=0;i<rows;i++){
        for (int j=0;j<cols;j++){
            fprintf(file, "%c", labyrinth->map[i][j]);
        }
        fprintf(file, "%c", '\n');
    }
    fclose(file);
    return true;
    //write back the map to map.txt after you move the player for next time
}


void dfs(Labyrinth *labyrinth, int row, int col, bool visited[MAX_ROWS][MAX_COLS]) {
    visited[row][col] = true;
    int next[4][2]={
        {row+1, col},
        {row-1, col},
        {row, col-1},
        {row, col+1}
    };
    for (int i=0;i<4;i++){
        if (next[i][0]>=0&&next[i][0]<labyrinth->rows){
            if (next[i][1]>=0&&next[i][1]<labyrinth->cols){
                if (!visited[next[i][0]][next[i][1]]){
                    if (labyrinth->map[next[i][0]][next[i][1]]!='#'){
                        dfs(labyrinth, next[i][0], next[i][1], visited);
                    }
                }
            }
        }
    }
    return ;
}

bool isConnected(Labyrinth *labyrinth) {
    bool visit[MAX_ROWS][MAX_COLS]={false};
    int rows = labyrinth->rows;
    int cols = labyrinth->cols;
    int x,y;
    for (int i=0;i<rows;i++){
        for (int j=0;j<cols;j++){
            if (labyrinth->map[i][j] != '#'){
                x = i;
                y = j;
                goto dfs;
            }
        }
    }
    return false;
dfs:
    dfs(labyrinth, x, y, visit);
    for (int i=0;i<rows;i++){
        for (int j=0;j<cols;j++){
            if (!visit[i][j] && labyrinth->map[i][j]!='#'){
                return false;
            }
        }
    }
    return true;
}
