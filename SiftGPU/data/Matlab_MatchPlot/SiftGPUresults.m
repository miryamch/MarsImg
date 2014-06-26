I1 = imread('../Image1/upleft.JP2');
I2 = imread('../Image2/shifted.JP2');
data = load('matching_points_5x5_ta1000_ts25.txt');
points1 = [data(:,1), data(:,2)]; 
points2 = [data(:,3), data(:,4)]; 
match_plot(I1, I2, points1, points2);