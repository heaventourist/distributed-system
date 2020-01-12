import java.util.*;
import java.io.*;

public class Test{
    public static void main(String[] args) {
        String[] res = "s/a".split("/");
        for(String s: res){
            System.out.println(s);
        }
        System.out.println(res.length);
    }
}